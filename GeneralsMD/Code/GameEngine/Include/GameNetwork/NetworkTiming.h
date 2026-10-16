/*
**	Command & Conquer Generals Zero Hour(tm)
**	Modified version - not the original program.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

///////////////////////////////////////////////////////////////////////////////////////////////////
// NetworkTiming.h
//
// Timing policy for the lockstep network layer: round trip estimation, retransmission timeouts
// and the run-ahead (input delay) calculation.
//
// The original code used
//   * a fixed 2 second retransmission timer, so a single lost UDP packet froze every player for
//     up to 2 seconds (the game cannot advance until a frame's commands from everyone arrived);
//   * an average-only latency for the run-ahead, so normal Wi-Fi jitter regularly exceeded the
//     cushion and stalled the game.
//
// This header replaces both with standard techniques:
//   * RttEstimator - Jacobson/Karels smoothed RTT + RTT variance (RFC 6298), used for an adaptive
//     retransmission timeout and for a jitter-aware latency figure;
//   * redundant sends - a synchronized command (frame info, game command, ...) is repeated in the
//     next NET_REDUNDANT_SENDS packet flushes until acked, so a single lost packet costs one flush
//     interval (~16 ms) instead of a full timeout.  The receiver already discards duplicates of
//     these command types (FrameData / NetCommandList), so this is safe;
//   * ComputeRunAhead - run-ahead from the worst one-way path latency including jitter, with
//     hysteresis so it does not oscillate.
//
// None of this affects the simulation, only *when* packets are sent and how many frames of input
// delay the game uses, so it needs no determinism guarantees.  The header only uses built-in
// types so it can be unit tested outside the game (see Tests/Determinism/networktiming_test.cpp).
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __NETWORKTIMING_H
#define __NETWORKTIMING_H

enum
{
	NET_RTO_INITIAL_MS					= 300,	///< retransmission timeout before any RTT sample (was a fixed 2000)
	NET_RTO_MIN_MS							= 80,		///< never retransmit faster than this
	NET_RTO_MAX_MS							= 1000,	///< never wait longer than this between retries
	NET_ACK_DELAY_ALLOWANCE_MS	= 50,		///< time the peer may take to answer (its update loop + send batching)
	NET_RETRY_MAX_BACKOFF_SHIFT	= 3,		///< back off at most 8x for a command that keeps getting lost
	NET_REDUNDANT_SENDS					= 2,		///< synchronized commands are repeated in this many following flushes
	NET_REDUNDANT_MIN_SPACING_MS	= 10,	///< minimum spacing between redundant copies

	NET_SENDS_PER_LOGIC_FRAME		= 2,		///< outgoing packets are flushed at least this often per logic frame
	NET_RUNAHEAD_PROCESSING_FRAMES	= 2,	///< frames of margin for batching/processing on both hops of the relay
	NET_RUNAHEAD_MAX_DECREASE		= 2,		///< run-ahead may only shrink this many frames per metrics update
	NET_JITTER_MULTIPLIER				= 2			///< latency used for run-ahead = srtt + NET_JITTER_MULTIPLIER * rttvar
};

/**
 * Smoothed round trip time estimator (RFC 6298 / Jacobson-Karels).
 * All values in milliseconds.
 */
class RttEstimator
{
public:
	RttEstimator() { reset(); }

	void reset()
	{
		m_hasSample = false;
		m_srtt = 0.0f;
		m_rttvar = 0.0f;
	}

	/// Feed one measured round trip.  Only feed samples from commands that were sent exactly once
	/// (Karn's algorithm): for a retransmitted command we cannot know which copy was acked.
	void addSample(float rttMs)
	{
		if (rttMs < 0.0f)
			rttMs = 0.0f;

		if (!m_hasSample)
		{
			m_srtt = rttMs;
			m_rttvar = rttMs * 0.5f;
			m_hasSample = true;
			return;
		}

		float err = rttMs - m_srtt;
		if (err < 0.0f)
			err = -err;
		m_rttvar = 0.75f * m_rttvar + 0.25f * err;
		m_srtt = 0.875f * m_srtt + 0.125f * rttMs;
	}

	bool hasSample() const						{ return m_hasSample; }
	float getSmoothedRtt() const			{ return m_srtt; }
	float getRttVariation() const			{ return m_rttvar; }

	/// Round trip time that is rarely exceeded: srtt + k * rttvar.
	float getConservativeRtt(float k) const
	{
		return m_srtt + k * m_rttvar;
	}

	/// Retransmission timeout in milliseconds.
	unsigned int getRetransmitTimeout() const
	{
		if (!m_hasSample)
			return NET_RTO_INITIAL_MS;

		float var = 4.0f * m_rttvar;
		if (var < (float)NET_ACK_DELAY_ALLOWANCE_MS)
			var = (float)NET_ACK_DELAY_ALLOWANCE_MS;

		float rto = m_srtt + var;
		if (rto < (float)NET_RTO_MIN_MS)
			rto = (float)NET_RTO_MIN_MS;
		if (rto > (float)NET_RTO_MAX_MS)
			rto = (float)NET_RTO_MAX_MS;
		return (unsigned int)rto;
	}

private:
	bool m_hasSample;
	float m_srtt;
	float m_rttvar;
};

/**
 * How long to wait before re-sending a command that has already been sent numSends times.
 *
 * Redundant commands (synchronized command types, whose duplicates the receiver discards) are
 * first repeated on the next NET_REDUNDANT_SENDS flushes.  After that - and for every other
 * command - the retransmission timeout applies, with capped exponential backoff so a dead link is
 * not flooded.
 */
inline unsigned int ComputeRetryInterval(unsigned int rtoMs, unsigned int groupingMs, int numSends, bool redundant)
{
	if (redundant && numSends <= NET_REDUNDANT_SENDS)
		return (groupingMs < (unsigned int)NET_REDUNDANT_MIN_SPACING_MS) ? (unsigned int)NET_REDUNDANT_MIN_SPACING_MS : groupingMs;

	int shift = numSends - 1;
	if (redundant)
		shift -= NET_REDUNDANT_SENDS;
	if (shift < 0)
		shift = 0;
	if (shift > NET_RETRY_MAX_BACKOFF_SHIFT)
		shift = NET_RETRY_MAX_BACKOFF_SHIFT;

	unsigned int interval = rtoMs << shift;
	if (interval > (unsigned int)NET_RTO_MAX_MS)
		interval = NET_RTO_MAX_MS;
	return interval;
}

/**
 * Whether the ack for a command sent numSends times may be used as a round trip sample.
 * Karn's algorithm: never sample a command that was retransmitted because of a timeout, since we
 * cannot tell which copy was acked.  Redundant copies are sent within a couple of flush intervals
 * of the first one, so timing from the first send only overestimates the RTT by that small,
 * bounded amount - which is the safe direction for a timeout.
 */
inline bool ShouldSampleRtt(int numSends, bool redundant)
{
	if (redundant)
		return numSends <= NET_REDUNDANT_SENDS + 1;
	return numSends == 1;
}

/**
 * Minimum time in milliseconds between two packet flushes on a connection.
 * The original code waited (runAhead / fps / 2) seconds - 166ms at the minimum run-ahead - which
 * used up half of the input-delay window before a command even left the machine.
 */
inline int ComputeFrameGroupingMs(int frameRate)
{
	if (frameRate < 1)
		frameRate = 1;
	int grouping = 1000 / (frameRate * NET_SENDS_PER_LOGIC_FRAME);
	if (grouping < 1)
		grouping = 1;
	return grouping;
}

/**
 * Compute the run-ahead (number of frames between issuing a command and executing it).
 *
 * @param pathLatencySec	worst one-way latency of a command (sender -> packet router -> receiver),
 *												in seconds, already including jitter.
 * @param fps							logic frame rate the game will run at.
 * @param slackPercent		extra safety margin in percent (GlobalData::m_networkRunAheadSlack).
 * @param oldRunAhead			current run-ahead, used for hysteresis (<= 0 means none).
 */
inline int ComputeRunAhead(float pathLatencySec, int fps, int slackPercent, int oldRunAhead,
													 int minRunAhead, int maxRunAhead)
{
	if (pathLatencySec < 0.0f)
		pathLatencySec = 0.0f;

	// frames needed to cover the latency, rounded up
	float framesF = pathLatencySec * (float)fps;
	int frames = (int)framesF;
	if ((float)frames < framesF)
		++frames;

	frames += NET_RUNAHEAD_PROCESSING_FRAMES;
	frames += (frames * slackPercent + 99) / 100;	// slack, rounded up

	// Grow immediately (stalls are worse than a little extra delay), shrink gently.
	if (oldRunAhead > 0 && frames < oldRunAhead - NET_RUNAHEAD_MAX_DECREASE)
		frames = oldRunAhead - NET_RUNAHEAD_MAX_DECREASE;

	if (frames < minRunAhead)
		frames = minRunAhead;
	if (frames > maxRunAhead)
		frames = maxRunAhead;
	return frames;
}

#endif // __NETWORKTIMING_H

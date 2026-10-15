// Unit tests for NetworkTiming.h plus a simple lockstep link simulation comparing the original
// timing policy with the new one.
//
// The simulation is a MODEL (3 players: wired host acting as packet router + 2 Wi-Fi laptops),
// not the game itself.  It reproduces the parts of the protocol that decide stalls:
// send batching ("frame grouping"), the packet-router relay hop, retransmission timeouts, ack
// delay, and the run-ahead feedback loop.
//
// Build: g++ -O2 -I../../GameEngine/Include networktiming_test.cpp -o networktiming_test

#include <stdio.h>
#include <math.h>
#include <vector>
#include <map>
#include "GameNetwork/NetworkTiming.h"

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { printf("  FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static void unitTests()
{
	RttEstimator e;
	EXPECT(e.getRetransmitTimeout() == NET_RTO_INITIAL_MS);
	for (int i = 0; i < 50; ++i) e.addSample(10.0f);
	EXPECT(e.getSmoothedRtt() > 9.9f && e.getSmoothedRtt() < 10.1f);
	EXPECT(e.getRetransmitTimeout() == NET_RTO_MIN_MS);					// LAN: clamps to the floor
	for (int i = 0; i < 50; ++i) e.addSample((i & 1) ? 40.0f : 120.0f);	// jittery link
	EXPECT(e.getRetransmitTimeout() > 150 && e.getRetransmitTimeout() < 400);
	for (int i = 0; i < 50; ++i) e.addSample(5000.0f);
	EXPECT(e.getRetransmitTimeout() == NET_RTO_MAX_MS);

	// non-redundant commands: RTO with capped exponential backoff
	EXPECT(ComputeRetryInterval(100, 16, 1, false) == 100);
	EXPECT(ComputeRetryInterval(100, 16, 2, false) == 200);
	EXPECT(ComputeRetryInterval(100, 16, 4, false) == 800);
	EXPECT(ComputeRetryInterval(100, 16, 9, false) == 800);				// backoff capped at 8x
	EXPECT(ComputeRetryInterval(400, 16, 9, false) == NET_RTO_MAX_MS);
	// redundant (synchronized) commands: repeated on the next flushes, then RTO
	EXPECT(ComputeRetryInterval(100, 16, 1, true) == 16);
	EXPECT(ComputeRetryInterval(100, 16, NET_REDUNDANT_SENDS, true) == 16);
	EXPECT(ComputeRetryInterval(100, 16, NET_REDUNDANT_SENDS + 1, true) == 100);
	EXPECT(ComputeRetryInterval(100, 16, NET_REDUNDANT_SENDS + 2, true) == 200);
	EXPECT(ComputeRetryInterval(100, 0, 1, true) == NET_REDUNDANT_MIN_SPACING_MS);
	EXPECT(ShouldSampleRtt(1, false) && !ShouldSampleRtt(2, false));
	EXPECT(ShouldSampleRtt(NET_REDUNDANT_SENDS + 1, true) && !ShouldSampleRtt(NET_REDUNDANT_SENDS + 2, true));

	EXPECT(ComputeFrameGroupingMs(30) == 16);
	EXPECT(ComputeFrameGroupingMs(0) >= 1);

	// LAN: tiny latency -> min run-ahead
	EXPECT(ComputeRunAhead(0.005f, 30, 10, 0, 6, 64) == 6);
	// 100 ms one-way -> 3 frames + 2 margin = 5, +10% = 6
	EXPECT(ComputeRunAhead(0.100f, 30, 10, 0, 4, 64) == 6);
	// grow immediately
	EXPECT(ComputeRunAhead(0.500f, 30, 10, 6, 6, 64) == 19);
	// shrink at most NET_RUNAHEAD_MAX_DECREASE per update
	EXPECT(ComputeRunAhead(0.0f, 30, 10, 20, 6, 64) == 20 - NET_RUNAHEAD_MAX_DECREASE);
	// clamp to max
	EXPECT(ComputeRunAhead(10.0f, 30, 10, 0, 6, 64) == 64);
}

// ---------------------------------------------------------------------------------------------
// link simulation
// ---------------------------------------------------------------------------------------------

static unsigned int g_seed = 1;
static double rnd() { g_seed = g_seed * 1664525u + 1013904223u; return (double)(g_seed >> 8) / 16777216.0; }

struct Link { double baseMs; double jitterMeanMs; double spikeChance; double spikeMs; double loss; };

static double oneWay(const Link &l)
{
	double d = l.baseMs - l.jitterMeanMs * log(1.0 - rnd() + 1e-12);
	if (rnd() < l.spikeChance) d += l.spikeMs * rnd();
	return d;
}

struct Policy
{
	bool isNew;
	int minRunAhead;
};

struct Sender	// one directed connection
{
	RttEstimator est;				// new policy
	std::vector<double> hist;		// old policy: plain average latency history
	double phase;					// flush phase offset
};

// Commands flushed together on the same connection travel in the same UDP packet, so they are
// lost (or delayed) together.  Remember each packet's fate.
struct PacketFate { bool lost; double delay; };
static std::map<long long, PacketFate> g_packets;
static PacketFate packetFate(int a, int b, double departTime, double loss, const Link &lk)
{
	long long key = ((long long)(departTime * 16.0) << 4) | (a << 2) | b;
	std::map<long long, PacketFate>::iterator it = g_packets.find(key);
	if (it != g_packets.end()) return it->second;
	PacketFate f;
	f.lost = rnd() < loss;
	f.delay = oneWay(lk);
	g_packets[key] = f;
	return f;
}

static double nextFlush(double t, double grouping, double phase)
{
	double k = ceil((t - phase) / grouping);
	return phase + k * grouping;
}

struct SimResult { double avgDelayMs; double frozenPct; double bigStallsPer10Min; double worstStallMs; };

static SimResult simulate(const Policy &pol, const Link links[3], double seconds)
{
	const int R = 0;			// player 0 is the packet router (host)
	const double framePeriod = 1000.0 / 30.0;
	const int frames = (int)(seconds * 30.0);
	int runAhead = 30;			// both versions start at 30 frames (Network.cpp)

	Sender snd[3][3];
	for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) snd[a][b].phase = rnd() * 50.0;

	std::vector<double> need(frames + 200, 0.0);	// latest arrival needed for frame f
	std::vector<double> T(frames + 1, 0.0);
	double now = 0.0, stall = 0.0, worst = 0.0, delaySum = 0.0;
	int bigStalls = 0;
	double nextMetrics = 500.0;

	for (int k = 0; k < frames; ++k)
	{
		double sched = (k == 0) ? 0.0 : T[k - 1] + framePeriod;
		double t = sched;
		if (need[k] > t) t = need[k];
		double s = t - sched;
		if (k > 0 && s > 0) { stall += s; if (s > worst) worst = s; if (s > 100) ++bigStalls; }
		T[k] = t;
		now = t;
		delaySum += runAhead * framePeriod;

		// frame grouping (ms between flushes)
		double clientGroup, routerGroup;
		if (pol.isNew) { clientGroup = ComputeFrameGroupingMs(30); }
		else { clientGroup = (1000.0 * runAhead / 30.0) / 2.0; if (clientGroup > 500) clientGroup = 500; }
		routerGroup = clientGroup / 2.0;

		// every player sends its frame message for execution frame k + runAhead
		int execFrame = k + runAhead;
		for (int src = 0; src < 3; ++src)
		{
			for (int dst = 0; dst < 3; ++dst)
			{
				if (dst == src) continue;
				// path: direct if router involved, else src -> R -> dst
				int hops[3]; int nh = 0;
				hops[nh++] = src;
				if (src != R && dst != R) hops[nh++] = R;
				hops[nh++] = dst;

				double ready = now;
				for (int h = 0; h + 1 < nh; ++h)
				{
					int a = hops[h], b = hops[h + 1];
					Sender &S = snd[a][b];
					double group = (a == R) ? routerGroup : clientGroup;
					// a link hop crosses the sender's and receiver's last mile
					Link lk;
					lk.baseMs = links[a].baseMs + links[b].baseMs;
					lk.jitterMeanMs = links[a].jitterMeanMs + links[b].jitterMeanMs;
					lk.spikeChance = links[a].spikeChance + links[b].spikeChance;
					lk.spikeMs = (links[a].spikeMs > links[b].spikeMs) ? links[a].spikeMs : links[b].spikeMs;
					lk.loss = links[a].loss + links[b].loss;

					double rto;
					double firstDepart;
					if (pol.isNew) rto = S.est.getRetransmitTimeout(); else rto = 2000.0;
					int attempt = 0;
					double depart = nextFlush(ready, group, S.phase);
					firstDepart = depart;
					double arrival;
					for (;;)
					{
						++attempt;
						PacketFate pf = packetFate(a, b, depart, lk.loss, lk);
						if (!pf.lost) { arrival = depart + pf.delay; break; }
						double wait = pol.isNew ? ComputeRetryInterval((unsigned)rto, (unsigned)group, attempt, true) : rto;
						depart = nextFlush(depart + wait, group, S.phase);
					}
					// ack returns: old acks wait for the receiver's flush; new acks go out on its next update
					double ackGroup = (b == R) ? routerGroup : clientGroup;
					double ackDepart = pol.isNew ? arrival + 5.0 * rnd() : nextFlush(arrival, ackGroup, snd[b][a].phase);
					double ackArrive = ackDepart + oneWay(lk);
					if (pol.isNew) { if (ShouldSampleRtt(attempt, true)) S.est.addSample((float)(ackArrive - firstDepart)); }
					else { double sample = ackArrive - depart; S.hist.push_back(sample); if (S.hist.size() > 200) S.hist.erase(S.hist.begin()); }
					ready = arrival;
				}
				if (execFrame < (int)need.size() && ready > need[execFrame]) need[execFrame] = ready;
			}
		}

		// run-ahead metrics, every 500 ms, computed by the router from the two worst client RTTs
		if (now >= nextMetrics)
		{
			nextMetrics = now + 500.0;
			double lat[3] = {0, 0, 0};
			for (int c = 1; c < 3; ++c)
			{
				Sender &S = snd[c][R];
				if (pol.isNew) lat[c] = S.est.hasSample() ? S.est.getConservativeRtt((float)NET_JITTER_MULTIPLIER) / 1000.0 : 0.2;
				else { double sum = 0; for (size_t i = 0; i < S.hist.size(); ++i) sum += S.hist[i]; lat[c] = S.hist.empty() ? 0.2 : sum / S.hist.size() / 1000.0; }
			}
			double maxLat = lat[1] + lat[2];
			if (pol.isNew)
				runAhead = ComputeRunAhead((float)(maxLat / 2.0), 30, 10, runAhead, pol.minRunAhead, 64);
			else
			{
				int ra = (int)((maxLat / 2.0) * 30.0);
				ra += (ra * 10) / 100;
				if (ra < pol.minRunAhead) ra = pol.minRunAhead;
				if (ra > 64) ra = 64;
				runAhead = ra;
			}
		}
	}

	SimResult r;
	r.avgDelayMs = delaySum / frames;
	r.frozenPct = 100.0 * stall / (seconds * 1000.0 + stall);
	r.bigStallsPer10Min = bigStalls * 600.0 / seconds;
	r.worstStallMs = worst;
	return r;
}

static void scenario(const char *name, const Link &laptop)
{
	Link wired = {0.5, 0.2, 0.0, 0.0, 0.0};
	Link links[3] = {wired, laptop, laptop};
	Policy oldP = {false, 10};
	Policy newP = {true, 6};
	const double secs = 1200.0;	// 20 minute game
	g_packets.clear(); g_seed = 42; SimResult o = simulate(oldP, links, secs);
	g_packets.clear(); g_seed = 42; SimResult n = simulate(newP, links, secs);
	printf("  %-34s | %6.0f ms %6.1f%% %8.1f %7.0f ms | %6.0f ms %6.1f%% %8.1f %7.0f ms\n", name,
		o.avgDelayMs, o.frozenPct, o.bigStallsPer10Min, o.worstStallMs,
		n.avgDelayMs, n.frozenPct, n.bigStallsPer10Min, n.worstStallMs);
}

int main()
{
	unitTests();
	printf("unit tests: %s\n\n", failures ? "FAILED" : "passed");

	printf("Link simulation, 3 players (wired host + 2 laptops), 20 minute game (model, not the game):\n");
	printf("  %-34s | %-37s | %-37s\n", "", "ORIGINAL timing policy", "NEW timing policy");
	printf("  %-34s | %9s %7s %8s %10s | %9s %7s %8s %10s\n", "laptop link", "inp.delay", "frozen", "stalls*", "worst", "inp.delay", "frozen", "stalls*", "worst");
	Link lanWired   = {0.5, 0.2, 0.000, 0,   0.000};
	Link wifiGood   = {2.0, 3.0, 0.002, 60,  0.002};
	Link wifiBusy   = {3.0, 8.0, 0.010, 120, 0.010};
	Link wifiBad    = {4.0, 15.0, 0.030, 200, 0.030};
	Link internet   = {25.0, 6.0, 0.005, 100, 0.005};
	scenario("wired LAN", lanWired);
	scenario("good Wi-Fi (0.2% loss)", wifiGood);
	scenario("busy Wi-Fi (1% loss, spikes)", wifiBusy);
	scenario("bad Wi-Fi (3% loss, big spikes)", wifiBad);
	scenario("internet ~50ms RTT (0.5% loss)", internet);
	printf("  * stalls longer than 100 ms per 10 minutes of play\n");

	return failures ? 1 : 0;
}

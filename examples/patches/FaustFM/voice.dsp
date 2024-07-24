import("stdfaust.lib");

// FM: Frequency modulation
FM(fc,fm,amp) = fm : os.osc : *(amp) : +(1) : *(fc) : os.osc;

process = FM(hslider("freq carrier", 880, 40, 5000, 1),
            hslider("freq modulation", 200, 10, 1000, 1),
            hslider("amp modulation", 0, 0, 1, 0.01)) * button("gate")
        <: _,_;
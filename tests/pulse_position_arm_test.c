#include "../program/FCM4E1553 Ethercat/User/EcatPulsePosition.h"

#define CHECK(condition, code) do { if (!(condition)) return (code); } while (0)

/* Executed as compiled ARM machine code, not a JavaScript reimplementation. */
int pulse_test_main(void)
{
    TEcatPulsePosition p;
    int32_t resolutions[] = {200,400,800,1600,3200,6400,12800,25600,
                            1000,2000,4000,5000,8000,10000,20000,40000,1001};
    int32_t targets[] = {1,-1,7,-7,1000,-1000};
    int64_t target;
    int32_t step;
    int32_t i, j, n;
    for (i = 0; i < 17; i++)
    {
        for (j = 0; j < 6; j++)
        {
            EcatPulse_Reset(&p, resolutions[i]);
            target = EcatPulse_TargetPhase(targets[j], p.resolution);
            for (n = 0; p.phase != target && n < 100000; n++)
            {
                step = EcatPulse_LimitStep(&p, target,
                    (target > p.phase) ? 16000 : -16000, 1U);
                CHECK(EcatPulse_Advance(&p, step), 1);
                CHECK((target >= 0) ? (p.phase <= target) :
                                     (p.phase >= target), 2);
            }
            CHECK(p.phase == target, 3);
            CHECK(EcatPulse_Position(&p) == targets[j], 4);
            CHECK(EcatPulse_LimitStep(&p, target, 16000, 1U) == 0, 5);
        }
    }
    EcatPulse_Reset(&p, 1000);
    CHECK(EcatPulse_Advance(&p, 7), 6);
    target = p.phase;
    CHECK(EcatPulse_Advance(&p, 0), 7);
    CHECK(p.phase == target && p.fraction == 7000, 8);
    CHECK(EcatPulse_Advance(&p, -7), 9);
    CHECK(p.phase == 0 && p.fraction == 0 && p.whole_pulses == 0, 10);

    /* 1.25 million logical pulses: beyond the positions in the stop screenshots. */
    EcatPulse_Reset(&p, 1000);
    for (n = 0; n < 1200000; n++)
        CHECK(EcatPulse_Advance(&p, 16000), 11);
    CHECK(p.phase == 19200000000LL, 12);
    CHECK(EcatPulse_Position(&p) == 1250000, 13);
    for (n = 0; n < 1200000; n++)
        CHECK(EcatPulse_Advance(&p, -16000), 14);
    CHECK(p.phase == 0 && EcatPulse_Position(&p) == 0 &&
          p.fraction == 0, 15);

    EcatPulse_Reset(&p, 1000);
    p.phase = EcatPulse_TargetPhase(INT32_MAX - 1, p.resolution);
    p.whole_pulses = INT32_MAX - 1;
    target = EcatPulse_TargetPhase(INT32_MAX, p.resolution);
    step = EcatPulse_LimitStep(&p, target, 16000, 0U);
    CHECK(step == 15360 && EcatPulse_Advance(&p, step), 16);
    CHECK(EcatPulse_Position(&p) == INT32_MAX, 17);
    CHECK(EcatPulse_LimitStep(&p, 0, 16000, 0U) == 0, 18);
    CHECK(!EcatPulse_Advance(&p, 1) && p.phase == target, 19);

    EcatPulse_Reset(&p, 1000);
    p.phase = EcatPulse_TargetPhase(INT32_MIN + 1, p.resolution);
    p.whole_pulses = INT32_MIN + 1;
    target = EcatPulse_TargetPhase(INT32_MIN, p.resolution);
    step = EcatPulse_LimitStep(&p, target, -16000, 0U);
    CHECK(step == -15360 && EcatPulse_Advance(&p, step), 20);
    CHECK(EcatPulse_Position(&p) == INT32_MIN, 21);
    CHECK(EcatPulse_LimitStep(&p, 0, -16000, 0U) == 0, 22);
    CHECK(!EcatPulse_Advance(&p, -1) && p.phase == target, 23);
    CHECK(!EcatPulse_Advance(&p, 32767), 24);
    return 0;
}


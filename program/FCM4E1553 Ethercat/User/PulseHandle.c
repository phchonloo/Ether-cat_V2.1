#include "PulseHandle.h"
#include "EthercatMotorControl.h"

s16 PuCount[2] = {0, 0};
s16 DrCount[2] = {0, 0};
s16 DrPulseNum = 0;
s16 PuPulseNum = 0;
s16 PU_add = 0;
s16 DR_add = 0;
s16 SumPulsePu = 0;
s16 SumPulseDr = 0;
s16 Dir_x = 1;
s32 SumStep = 0;
s32 SpeedNow = 0;
s32 BaseRpm = 0;
s16 Mean_i = 1;
s32 xifen = 1000;

s32 TestSpeed = 0;
s32 SpeedStar = 1;
s32 WorkSpeedMax = 1;
s32 SpeedAdd;
s32 SpeedIndex;
s32 SpeedMin;
s32 gongzuo_shiji;
s32 gongzuo_half;
s32 gongzuo_stop;

u8 SumPu = 0;
u8 SumDr = 0;
u8 PuIo[8] = {0, 0, 0, 0, 0, 0, 0, 0};
u8 DrIo[8] = {0, 0, 0, 0, 0, 0, 0, 0};
u8 Ioi = 0;
u8 PuIoF = 0;
u8 DrIoF = 0;
u8 RunF = 0;
u8 PuIoF1 = 0;
u8 DrIoF1 = 0;

/* Called by MainFunt() on every 16 kHz motor-control cycle. */
void PulseFunction(void)
{
    /*
     * Checking AL state here is the final fail-safe: even if the foreground
     * loop stalls, losing EtherCAT OP clears the motor command immediately.
     */
    if ((gEcatMotorEnabled != 0U) &&
        ((nAlStatus & STATE_MASK) == STATE_OP))
    {
        BaseRpm = EthercatMotorControl_ApplyRamp(
            gEcatBaseRpmCommand);
    }
    else
    {
        EthercatMotorControl_EmergencyStop();
        BaseRpm = 0;
    }

    EthercatMotorControl_FastUpdate(BaseRpm);
}

void RAMRUN WorkSelf(void)
{
    s32 local_speed_index;
    s32 speed_acc;
    s32 speed_low;
    s32 work_speed_max;
    s32 spin;

    local_speed_index = XiFen();
    speed_low = WorkValue[9] << 4;
    speed_acc = WorkValue[8] << 4;
    SpeedStar = WorkValue[6] << 4;
    work_speed_max = speed_low + speed_acc * local_speed_index;
    WorkSpeedMax = work_speed_max;

    spin = adc_value[2];
    if (spin < WorkValue[10])
    {
        spin = 0;
    }

    if (((PuIoF == 0U) && (WorkValue[4] == 0)) ||
        ((WorkValue[4] == 1) && (DrIoF == 0U) && (PuIoF == 0U)))
    {
        TestSpeed -= WorkValue[7];
        if (TestSpeed <= SpeedStar)
        {
            TestSpeed = 0;
        }
    }
    else
    {
        Dir_x = (DrIoF == 1U) ? -1 : 1;

        if (TestSpeed > WorkSpeedMax)
        {
            TestSpeed -= WorkValue[7];
        }
        else if (TestSpeed < WorkSpeedMax)
        {
            TestSpeed += WorkValue[7];
        }
    }

    BaseRpm = TestSpeed * Dir_x;
}

/* WolkMode > 2: run one fixed travel profile. */
void RAMRUN PulseSet(void)
{
    u32 pulse_count;
    u32 travel_low;
    u64 travel_value;

    if ((PuIoF1 == 0U) && (PuIoF == 1U) && (TestSpeed == 0))
    {
        RunF = 1U;
        Dir_x = 1;
    }
    else if ((DrIoF1 == 0U) && (DrIoF == 1U) &&
             (TestSpeed == 0))
    {
        RunF = 1U;
        Dir_x = -1;
    }

    if (RunF == 1U)
    {
        pulse_count = 15360000U / sxifen;
        travel_low = WorkValue[9];
        travel_value = ((u64)travel_low * 10000U) + WorkValue[10];
        gongzuo_shiji = (s32)(travel_value * pulse_count);
        gongzuo_half = gongzuo_shiji >> 1;
        gongzuo_stop = 0;
        WorkSpeedMax = WorkValue[8] << 4;
        SpeedStar = WorkValue[6] << 4;
        RunF = 2U;
    }
    else if (RunF == 2U)
    {
        if ((TestSpeed < WorkSpeedMax) &&
            (gongzuo_stop < gongzuo_half))
        {
            TestSpeed += WorkValue[7];
            gongzuo_stop += TestSpeed;
        }
        else
        {
            RunF = 3U;
        }
    }
    else if ((RunF == 3U) &&
             (gongzuo_shiji <= gongzuo_stop))
    {
        RunF = 4U;
    }
    else if (RunF == 4U)
    {
        if (TestSpeed > SpeedStar)
        {
            TestSpeed -= WorkValue[7];
        }
        if (gongzuo_shiji <= TestSpeed)
        {
            TestSpeed = gongzuo_shiji;
        }
        if (gongzuo_shiji <= 0)
        {
            RunF = 0U;
            TestSpeed = 0;
        }
    }

    BaseRpm = TestSpeed * Dir_x;
    gongzuo_shiji -= TestSpeed;
}

void PuIoIntHandle(void)
{
}

void DrIoIntHandle(void)
{
}

void DataIni(void)
{
}

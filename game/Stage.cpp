#include "Stage.h"
#include "GameInfo.h"

// GLOBAL: CMR2 0x00542cac
char g_stageSplitCount = 0;
// GLOBAL: CMR2 0x00542cad
char g_unk0x00542cad;
// GLOBAL: CMR2 0x00542cae
char g_stageLooped;
// GLOBAL: CMR2 0x00542cb0
int g_primaryStartDriver;
// GLOBAL: CMR2 0x00542cb4
int g_playerStartLane[8];     // per player: start lane (0/1) of the super special
// GLOBAL: CMR2 0x00542c70
int g_stageCheckpointCount;
// GLOBAL: CMR2 0x00542c74
int g_splitPositionCount;
// GLOBAL: CMR2 0x00542c7c
int g_splitPositions[12];

// FUNCTION: CMR2 0x004583c0
int GetStageSplitCount(void)
{
	return g_stageSplitCount;
}

// FUNCTION: CMR2 0x00458390
int Stage_GetDriverCount(void)
{
    return g_stageArchiveTables.driverCount;
}

// FUNCTION: CMR2 0x004583a0
int Stage_GetSplitPositionCount(void)
{
    return g_splitPositionCount;
}

// FUNCTION: CMR2 0x004583b0
int Stage_GetSplitPositionFixed(int index)
{
    return g_splitPositions[index];
}

// GLOBAL: CMR2 0x00542d38
int g_unk0x00542d38[8];

#include "Car.h"
void RallyData_SetCarRaceRecordPosition(Car *pCar, int value);

// Start positions (x, -, z) of the cars when the rally modes use fixed grids.
// GLOBAL: CMR2 0x00542cd8
int g_fixedGridStartPositions[8][3];

unsigned char RallyData_GetSelectionFlag26(void);
unsigned int RallyData_GetFlag31(void);
unsigned int RallyData_GetSelectionFlag27(void);
// PORT: for the grid start of the "contact" split screen.
#include <math.h>
unsigned char RallyDataState(void);
int RallyData_GetRouteAvailabilityState(void);
void RallyData_GetRouteNodeGroundPosition(int index, int *pOut);
int StageObject_IsSplitContact(void);

// Start x/z of a car: from the grid table, or one of the stage's start
// points (pStarts, 3 ints each) depending on the mode.
// FUNCTION: CMR2 0x004583d0
void Stage_GetCarStartXZ(int car, int *pStarts, int *pOut)
{
    int i;

    if (!(BYTE)RallyData_GetSelectionFlag26() && !(BYTE)RallyData_GetFlag31()) {
        // PORT: in the "contact" split screen the player cars share the stage,
        // so they start on a grid of two rows (cars 1-2 side by side at the
        // start point, 3-4 six metres behind), laid along the route. In the
        // ghost split screen (the original's) they all start at the point.
        if (!(BYTE)RallyData_GetSelectionFlag27() && StageObject_IsSplitContact() && car > 0 &&
            car < (int)(BYTE)RallyDataState()) {
            int p0[3];
            int p1[3];
            int nodes = RallyData_GetRouteAvailabilityState();
            int k = 1;
            double dx = 0.0;
            double dz = 0.0;
            double len;
            double back = 9.0 * (car / 2);                 // row
            double side = (car & 1) ? 2.6 : -2.6;          // column

            RallyData_GetRouteNodeGroundPosition(0, p0);
            while (k < nodes) {
                RallyData_GetRouteNodeGroundPosition(k, p1);
                dx = (double)(p1[0] - p0[0]) / 65536.0;
                dz = (double)(p1[2] - p0[2]) / 65536.0;
                if (dx * dx + dz * dz >= 4.0)
                    break;
                k++;
            }
            len = sqrt(dx * dx + dz * dz);
            pOut[0] = pStarts[0];
            pOut[2] = pStarts[2];
            if (len > 0.0) {
                dx /= len;
                dz /= len;
                pOut[0] += (int)((-dx * back + dz * side) * 65536.0);
                pOut[2] += (int)((-dz * back - dx * side) * 65536.0);
            }
            return;
        }
        if ((BYTE)RallyData_GetSelectionFlag27()) {
            if (CGameInfo::GetGameModeOptionBit19()) {
                i = g_playerStartLane[car];
                pOut[0] = pStarts[i * 3];
                pOut[2] = pStarts[i * 3 + 2];
                return;
            }
            if (car != g_primaryStartDriver) {
                pOut[0] = pStarts[3];
                pOut[2] = pStarts[5];
                return;
            }
        }
        pOut[0] = pStarts[0];
        pOut[2] = pStarts[2];
        return;
    }
    pOut[0] = g_fixedGridStartPositions[car][0];
    pOut[2] = g_fixedGridStartPositions[car][2];
}

// Puts every car back on its stored route position.
// FUNCTION: CMR2 0x00458480
void Stage_RestoreCarsToRoutePositions(void)
{
    int i;

    for (i = 0; i < g_stageArchiveTables.driverCount; i++) {
        Car *pCar = Car_Get(i);
        RallyData_SetCarRaceRecordPosition(pCar, g_unk0x00542d38[i] >> 16);
    }
}

// FUNCTION: CMR2 0x004584c0
int Stage_GetPrimaryStartDriver(void)
{
    return g_primaryStartDriver;
}

// Previous checkpoint, wrapping round on looped stages.
// FUNCTION: CMR2 0x00459320
int Stage_GetPreviousCheckpoint(int index)
{
    int result = index - 1;
    if (result == -1) {
        if (g_stageLooped != 0) {
            index = g_stageCheckpointCount;
            result = index - 1;
        } else {
            result = 0;
        }
    }
    return result;
}

// Next checkpoint, wrapping round on looped stages.
// FUNCTION: CMR2 0x00459350
int Stage_GetNextCheckpoint(int index)
{
    index++;
    if (index == g_stageCheckpointCount && g_stageLooped != 0)
        index = 0;
    return index;
}


// Start-grid side flag used by the stage start layout (0/1).
// GLOBAL: CMR2 0x00542cd4
int g_startGridSide;

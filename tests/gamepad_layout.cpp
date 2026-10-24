// Gamepads reach the game as DirectInput showed an Xbox controller, and
// triggers and pedals as DirectInput wheels report pedals: the maximum when
// released, the minimum when pressed fully (input_sdl.cpp). Uses SDL virtual
// devices, so no hardware or display is needed. The game window never has the
// keyboard focus here, as under gamescope: controllers must still be read.

#include "port/types.h"
#include "port/input.h"
#include "Input.h"
#include "Menu.h"
#include "platform/platform.h"
#include "platform/testing.h"
#include "main.h"
#include "Car.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

extern void Input_TranslatePedalsToMenuKeys();
extern void Input_MergeAssignedJoystickButtons(int slot, DeviceInfo *out);
extern void Input_RescanChangedJoysticks();
extern unsigned short Input_GetAssignedController();
extern BYTE g_carSimulationClockFlags;
extern int Car_UpdateEngineNoteFalloff(int *);

namespace {

int s_failures;

void Check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        s_failures++;
    }
}

const LONG kRange = 1000;   // the game sets -range..range on each axis it uses

InputJoystickState Read(InputDevice *device)
{
    InputJoystickState state;
    Check(Input_ReadJoystick(device, &state) != FALSE, "joystick reads");
    return state;
}

unsigned MenuInput(DeviceInfo *pad)
{
    CInput::UpdateDevice(int(pad - CInput::m_availableDevices));
    Input_TranslatePedalsToMenuKeys();
    DeviceInfo keyboard{};
    Input_MergeAssignedJoystickButtons(0, &keyboard);
    return keyboard.field_0x8;
}

void CheckMenuNavigation(SDL_Joystick *virt, DeviceInfo *pad)
{
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
        SDL_SetJoystickVirtualAxis(virt, i, i >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? SDL_JOYSTICK_AXIS_MIN : 0);
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        SDL_SetJoystickVirtualButton(virt, i, false);
    CInput::m_unk0x005168f4[0] = 2;
    Check((MenuInput(pad) & 0xf) == 0, "centred gamepad does not navigate");

    const struct { SDL_GamepadAxis axis; Sint16 value; unsigned direction; } stick[] = {
        {SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN, 4},
        {SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MAX, 8},
        {SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MIN, 1},
        {SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX, 2},
    };
    for (const auto &move : stick) {
        SDL_SetJoystickVirtualAxis(virt, move.axis, move.value);
        Check((MenuInput(pad) & 0xf) == move.direction, "left stick navigates menus in every direction");
        SDL_SetJoystickVirtualAxis(virt, move.axis, 0);
        MenuInput(pad);
    }
    const struct { SDL_GamepadButton button; unsigned direction; } dpad[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, 4}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 8},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 1}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 2},
    };
    for (const auto &move : dpad) {
        SDL_SetJoystickVirtualButton(virt, move.button, true);
        Check((MenuInput(pad) & 0xf) == move.direction, "D-pad navigates menus in every direction");
        SDL_SetJoystickVirtualButton(virt, move.button, false);
        MenuInput(pad);
    }

    Menu menu{};
    Menu_Init(&menu, nullptr, 0, 0, nullptr, nullptr, 1, 1, 1);
    for (int i = 0; i < 3; ++i)
        Menu_AddItemType1(&menu, nullptr, -1, nullptr, short(i));
    Menu_SetInputStateFlag(0); // no audio resources in this test
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN);
    Menu_Update(&menu, MenuInput(pad));
    Check(menu.cursor == 0, "stick input moves the real menu cursor up");
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFTY, 0);
    MenuInput(pad);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
    Menu_Update(&menu, MenuInput(pad));
    Check(menu.cursor == 1, "D-pad input moves the real menu cursor down");
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
    MenuInput(pad);

    for (SDL_GamepadAxis trigger : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
        SDL_SetJoystickVirtualAxis(virt, trigger, SDL_JOYSTICK_AXIS_MAX);
        Check((MenuInput(pad) & 0xf) == 0, "a trigger does not navigate menus");
        SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN);
        Check((MenuInput(pad) & 0xf) == 4, "held trigger does not swallow stick navigation");
        SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFTY, 0);
        SDL_SetJoystickVirtualAxis(virt, trigger, SDL_JOYSTICK_AXIS_MIN);
        Check((MenuInput(pad) & 0xf) == 0, "releasing a trigger does not navigate menus");
    }
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    Check((MenuInput(pad) & 0xf) == 0, "both triggers do not navigate menus");
    Check(pad->joystick.bindings[2].position < -39321 && pad->joystick.bindings[5].position < -39321,
          "menu polling preserves both driving pedals");
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    MenuInput(pad);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_SOUTH, true);
    Check((MenuInput(pad) & 0x30) == 0x10, "A confirms in menus");
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_SOUTH, false);
    MenuInput(pad);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, true);
    Check((MenuInput(pad) & 0x30) == 0x20, "B goes back in menus");
}

void CheckRacePresses(SDL_Joystick *virt, DeviceInfo *pad)
{
    BYTE keys[256] = {};
    Platform_SetTestKeyboard(keys);
    CInput::m_unk0x0059f8cc.field_0x0 = 0;
    CInput::SetupKeyboard();
    CInput::m_unk0x0059f8cc.field_0x0 = 3;
    const unsigned padBit = 1u << int(pad - CInput::m_availableDevices);
    const unsigned actionBits = 0x20 | 0x40 | 0x80 | 0x400;
    keys[0x1b] = keys[0x1a] = keys[0x2e] = keys[INPUT_KEY_ESCAPE] = 0;
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, false);
    CInput::UpdateAllAvailableDevices();

    // The real mixed-rate scheduler supplies opponent-only steps, as on a
    // super special. Inject input exactly on those steps at common frame rates.
    Car cars[2] = {};
    Car *oldBuffer = g_carBuffer;
    const short oldCount = g_carOrderCount;
    short oldOrder[2] = {g_carOrder[0], g_carOrder[1]};
    g_carBuffer = cars;
    g_carOrderCount = 2;
    g_carOrder[0] = 0; g_carOrder[1] = 1;
    for (unsigned fps : {30u, 60u, 120u, 144u, 240u}) {
        memset(cars, 0, sizeof(cars));
        cars[0].simulationRateHz = 25.f;
        cars[1].simulationRateHz = 50.f;
        g_carSimulationClockFlags = 0;
        CMain::m_frameTime = 1000;
        int phase;
        Car_UpdateEngineNoteFalloff(&phase);
        memset(keys, 0, sizeof(keys));
        SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, false);
        CInput::UpdateAllAvailableDevices();
        int injected = 0, delivered = 0;
        bool armed = true, pending = false;
        for (unsigned frame = 1; frame <= fps * 3; ++frame) {
            CMain::m_frameTime = 1000 + frame * 1000 / fps;
            int steps = Car_UpdateEngineNoteFalloff(&phase);
            for (int step = 0; step < steps; ++step) {
                bool playerStep = cars[0].simulationStepsRemaining > 0;
                bool press = !playerStep && armed;
                keys[0x1b] = keys[0x1a] = keys[0x2e] = keys[INPUT_KEY_ESCAPE] = press ? 0x80 : 0;
                SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, press);
                if (press) { ++injected; armed = false; pending = true; }
                CInput::UpdateRaceDevices(playerStep ? 1u | padBit : 0u);
                unsigned keyboard = CInput::m_availableDevices[0].field_0x8 & actionBits;
                unsigned gamepad = pad->field_0x8 & 0x20;
                if (!playerStep) Check(keyboard == 0 && gamepad == 0, "opponent does not consume player presses");
                else {
                    Check(keyboard == (pending ? actionBits : 0), "gear/camera/pause edges reach player exactly once");
                    Check(gamepad == (pending ? 0x20u : 0), "gamepad edge reaches player after release");
                    if (pending) { ++delivered; pending = false; }
                    armed = true;
                }
                for (Car &car : cars) if (car.simulationStepsRemaining > 0) --car.simulationStepsRemaining;
            }
        }
        Check(injected > 0 && delivered >= injected - 1, "mixed-rate scheduler exercises lost-input path");
    }
    // Independent devices keep their edges until the corresponding player
    // runs, and holding a button must not repeatedly shift gears.
    memset(keys, 0, sizeof(keys));
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, false);
    CInput::UpdateAllAvailableDevices();
    keys[0x1b] = keys[CInput::m_availableDevices[1].keyboard.field_0x46d] = 0x80;
    CInput::UpdateRaceDevices(0);
    CInput::UpdateRaceDevices(1);
    Check((CInput::m_availableDevices[0].field_0x8 & 0x20) != 0 &&
          (CInput::m_availableDevices[1].field_0x8 & 0x20) == 0, "player one does not consume player two presses");
    CInput::UpdateRaceDevices(2);
    Check((CInput::m_availableDevices[1].field_0x8 & 0x20) != 0, "player two receives deferred press");
    for (int i = 0; i < 10; ++i) {
        CInput::UpdateRaceDevices(3);
        Check(((CInput::m_availableDevices[0].field_0x8 | CInput::m_availableDevices[1].field_0x8) & 0x20) == 0,
              "held gear buttons never repeat");
    }
    memset(keys, 0, sizeof(keys));
    CInput::UpdateAllAvailableDevices();
    // Pending actions cannot leak out of a race into a frontend/pause menu.
    keys[0x1b] = 0x80;
    CInput::UpdateRaceDevices(0);
    CInput::UpdateAllAvailableDevices();
    keys[0x1b] = 0;
    CInput::UpdateRaceDevices(1);
    Check((CInput::m_availableDevices[0].field_0x8 & 0x20) == 0, "frontend clears pending race actions");
    Platform_SetTestKeyboard(nullptr);
    g_carBuffer = oldBuffer;
    g_carOrderCount = oldCount;
    g_carOrder[0] = oldOrder[0]; g_carOrder[1] = oldOrder[1];
}

void CheckCustomMapping(SDL_JoystickID id, SDL_Joystick *virt)
{
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, false);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_SOUTH, false);
    Check(Input_ApplyGamepadMapping(id, "a:b1,b:b0"), "custom button swap applies");
    InputDevice *custom = Input_OpenJoystick(0);
    Check(custom && Input_IsGamepad(custom), "custom layout opens as a gamepad");
    if (!custom) return;
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, true);
    InputJoystickState state = Read(custom);
    Check(state.rgbButtons[0] && !state.rgbButtons[1], "remapped confirm has no old action");
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    Check(Read(custom).lRz < -30000, "button override preserves automatic analog triggers");
    Input_CloseDevice(custom);
    Check(Input_ApplyGamepadMapping(id, "a:b0,b:b1"), "automatic buttons can be restored");
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_EAST, false);
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
}

void CheckUnknownController()
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    desc.name = "Unmapped handheld";
    desc.naxes = 2;
    desc.nbuttons = 36;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick *virt = SDL_OpenJoystick(id);
    Check(virt && !SDL_IsGamepad(id), "unknown handheld initially has no SDL gamepad layout");
    Check(Input_ApplyGamepadMapping(id, "a:b20,b:b21,leftx:a0,lefty:a1,lefttrigger:b15,righttrigger:b16"),
          "a manual profile adds an unknown controller");
    InputDevice *device = Input_OpenJoystick(0);
    Check(device && Input_IsGamepad(device), "unknown handheld becomes a standard gamepad");
    if (device) {
        InputJoystickState state = Read(device);
        Check(state.lZ > 30000 && state.lRz > 30000, "digital triggers initially released");
        SDL_SetJoystickVirtualButton(virt, 20, true);
        SDL_SetJoystickVirtualButton(virt, 15, true);
        SDL_SetJoystickVirtualButton(virt, 16, true);
        SDL_SetJoystickVirtualAxis(virt, 0, SDL_JOYSTICK_AXIS_MIN);
        state = Read(device);
        Check(state.rgbButtons[0] && !state.rgbButtons[1], "generic handheld button confirms");
        Check(state.lZ < -30000 && state.lRz < -30000, "digital handheld triggers operate independently");
        Check(state.lX < -30000, "manual profile preserves analog steering");
        Input_CloseDevice(device);
    }
    SDL_CloseJoystick(virt);
    SDL_DetachVirtualJoystick(id);
}

void CheckWheelMenuPedals(InputDevice *wheel, DeviceInfo *info)
{
    // A raw wheel keeps the original pedal navigation. Use the game's
    // polled axis values to exercise press/release latching deterministically.
    CInput::m_unk0x0059f6b0[info->field_0x18] = wheel;
    ControllerData &slot = CInput::m_controllerInfo[2];
    slot.field_0x2d8[2] = 2;
    slot.field_0x2d8[3] = 1;
    info->joystick.bindings[2].position = -0x10000;
    info->joystick.bindings[1].position = 0x10000;
    info->field_0x8 = 0x10;
    Input_TranslatePedalsToMenuKeys();
    Check(info->field_0x8 == 0x14, "wheel throttle navigates and preserves confirm");
    Input_TranslatePedalsToMenuKeys();
    Check(info->field_0x8 == 0x10, "held wheel pedal is latched once");
    info->joystick.bindings[2].position = 0x10000;
    info->joystick.bindings[1].position = -0x10000;
    Input_TranslatePedalsToMenuKeys();
    Check(info->field_0x8 == 0x18, "wheel brake navigates in the opposite direction");
    info->joystick.bindings[1].position = 0x10000;
    Input_TranslatePedalsToMenuKeys();
    Check(info->field_0x8 == 0x10, "released wheel pedals do not navigate");
    CInput::m_unk0x0059f6b0[info->field_0x18] = nullptr;
}

} // namespace

int main()
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("unfocused", 64, 64, SDL_WINDOW_HIDDEN);
    Check(window != NULL && SDL_GetKeyboardFocus() == NULL, "a game window without the keyboard focus");
    // An Xbox 360 controller: SDL knows its layout, as for a real one.
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x045e;
    desc.product_id = 0x028e;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Virtual Xbox 360 Controller";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick *virt = SDL_OpenJoystick(id);
    if (virt == NULL) {
        fprintf(stderr, "virtual joystick: %s\n", SDL_GetError());
        return 1;
    }
    // Released triggers rest at their minimum, as on real controllers.
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_UpdateJoysticks();

    Check(Input_GetJoystickCount() == 1, "one joystick");
    Check(Input_JoysticksChanged() != FALSE, "a joystick not opened yet is a change");
    InputDevice *device = Input_OpenJoystick(0);
    if (device == NULL) {
        fprintf(stderr, "Input_OpenJoystick failed\n");
        return 1;
    }
    Check(Input_JoysticksChanged() == FALSE, "no change once every joystick is open");
    InputCaps caps;
    Input_GetCaps(device, &caps);
    Check(caps.axes == 6 && caps.buttons == 10 && caps.povs == 1, "Xbox layout: 6 axes, 10 buttons, 1 POV");
    for (DWORD offset = 0; offset < 6 * 4; offset += 4)
        Input_SetAxisRange(device, offset, -kRange, kRange);

    Check(Input_IsGamepad(device) != FALSE, "recognised as a gamepad");
    InputJoystickState s = Read(device);
    Check(s.lZ == kRange && s.lRz == kRange, "released triggers read as released pedals");
    Check(s.lX == 0 && s.lY == 0 && s.lRx == 0 && s.lRy == 0, "sticks are centred");
    Check(s.rgdwPOV[0] == INPUT_POV_CENTERED, "D-pad centred");

    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    SDL_UpdateJoysticks();
    s = Read(device);
    Check(s.lRz == -kRange && s.lZ == kRange, "right trigger moves Rz alone");

    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    SDL_UpdateJoysticks();
    s = Read(device);
    Check(s.lZ == -kRange && s.lRz == -kRange, "both triggers held at once");

    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_UpdateJoysticks();
    s = Read(device);
    Check(s.lRz == kRange && s.lZ == -kRange, "released right trigger is released again");

    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(virt, SDL_GAMEPAD_AXIS_RIGHTY, SDL_JOYSTICK_AXIS_MAX);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_START, true);
    SDL_SetJoystickVirtualButton(virt, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
    SDL_UpdateJoysticks();
    s = Read(device);
    Check(s.lX == -kRange && s.lRy == kRange, "left stick X and right stick Y");
    Check(s.rgbButtons[0] == 0x80 && s.rgbButtons[7] == 0x80 && s.rgbButtons[1] == 0, "A is button 0, Start button 7");
    Check(s.rgdwPOV[0] == 9000, "D-pad right is POV 90 degrees");

    char name[64];
    Check(Input_GetObjectName(device, 2 * 4, name, sizeof(name)) && strcmp(name, "Z Axis") == 0, "Z axis named");

    // The game's controller table has room for four devices.
    SDL_VirtualJoystickDesc extra;
    SDL_INIT_INTERFACE(&extra);
    extra.naxes = 2;
    extra.nbuttons = 4;
    extra.name = "Virtual Extra Joystick";
    SDL_JoystickID extras[4];
    for (SDL_JoystickID &extraId : extras)
        extraId = SDL_AttachVirtualJoystick(&extra);
    Check(Input_JoysticksChanged() != FALSE, "a connected joystick is a change");
    Check(Input_GetJoystickCount() == 4, "at most four joysticks reach the game");
    for (SDL_JoystickID extraId : extras)
        SDL_DetachVirtualJoystick(extraId);
    Check(Input_JoysticksChanged() == FALSE, "disconnecting them restores the opened set");
    Input_CloseDevice(device);
    Check(Input_JoysticksChanged() != FALSE, "a connected joystick that is not open is a change");

    // The game's own detection gives a gamepad throttle on the right trigger
    // (Rz, axis 5) and brake on the left (Z, axis 2), as separate pedals.
    // As at startup, device slots 0 and 1 are reserved for keyboards.
    CInput::m_unk0x0059f8cc.field_0x0 = 2;
    CInput::GetAttachedJoysticks();
    DeviceInfo *pad = NULL;
    for (int i = 0; i < 8 && pad == NULL; i++)
        if (CInput::m_availableDevices[i].field_0x0 == 3)
            pad = &CInput::m_availableDevices[i];
    Check(pad != NULL, "the game set up the gamepad");
    if (pad != NULL) {
        CInput::InitDetectedControllerSlot(pad, 2);
        const ControllerData &slot = CInput::m_controllerInfo[2];
        Check(slot.field_0x210[0].present != 0 && slot.field_0x2d8[0] == 0, "default steering on the left stick");
        Check(slot.field_0x210[2].present != 0 && slot.field_0x2d8[2] == 5, "default throttle on the right trigger");
        Check(slot.field_0x210[3].present != 0 && slot.field_0x2d8[3] == 2, "default brake on the left trigger");
        CheckMenuNavigation(virt, pad);
        CheckRacePresses(virt, pad);
        Input_CloseDevice(CInput::m_unk0x0059f6b0[pad->field_0x18]);
        CInput::m_unk0x0059f6b0[pad->field_0x18] = NULL;
    }

    CheckCustomMapping(id, virt);
    SDL_CloseJoystick(virt);
    SDL_DetachVirtualJoystick(id);

    // A wheel whose pedal axis rests at its minimum, as some report it.
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_WHEEL;
    desc.naxes = 3;
    desc.name = "Virtual Wheel";
    id = SDL_AttachVirtualJoystick(&desc);
    virt = SDL_OpenJoystick(id);
    SDL_SetJoystickVirtualAxis(virt, 2, SDL_JOYSTICK_AXIS_MIN);
    SDL_UpdateJoysticks();
    device = Input_OpenJoystick(0);
    if (device == NULL) {
        fprintf(stderr, "Input_OpenJoystick failed for the wheel\n");
        return 1;
    }
    Check(Input_IsGamepad(device) == FALSE, "a wheel is not a gamepad");
    for (DWORD offset = 0; offset < 3 * 4; offset += 4)
        Input_SetAxisRange(device, offset, -kRange, kRange);
    s = Read(device);
    Check(s.lZ == kRange && s.lX == 0, "released pedal reads released, wheel centred");
    SDL_SetJoystickVirtualAxis(virt, 2, SDL_JOYSTICK_AXIS_MAX);
    SDL_UpdateJoysticks();
    s = Read(device);
    Check(s.lZ == -kRange, "pressed pedal reads pressed");
    if (pad != nullptr)
        CheckWheelMenuPedals(device, pad);
    Input_CloseDevice(device);
    SDL_CloseJoystick(virt);
    SDL_DetachVirtualJoystick(id);

    // A wheel connected after startup, or one that reconnects as it leaves
    // its generic USB mode, is listed once the controls menu is entered.
    CInput::RebuildAvailableInputDevices();
    CInput::RefreshControllerConfigurations();
    const unsigned short before = Input_GetAssignedController();
    desc.name = "Virtual T150";
    desc.nbuttons = 13;
    id = SDL_AttachVirtualJoystick(&desc);
    Input_RescanChangedJoysticks();
    Check(Input_GetAssignedController() == before + 1 &&
          strcmp(CInput::m_controllerInfo[before].name, "Virtual T150") == 0,
          "a wheel connected later is listed in the controls menu");
    SDL_DetachVirtualJoystick(id);
    Input_RescanChangedJoysticks();
    Check(Input_GetAssignedController() == before && CInput::m_unk0x0059f6b0[0] == NULL,
          "a disconnected wheel is closed and no longer listed");
    CInput::DInputReleaseDevices();

    CheckUnknownController();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (s_failures == 0)
        puts("gamepad layout: input, mixed-rate race edges, profiles, analog/digital triggers, menus and hotplug passed");
    return s_failures == 0 ? 0 : 1;
}

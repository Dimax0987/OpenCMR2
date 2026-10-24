package io.github.pablocpas.opencmr2

import android.app.Activity
import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import com.swordfish.radialgamepad.library.RadialGamePad
import com.swordfish.radialgamepad.library.config.ButtonConfig
import com.swordfish.radialgamepad.library.config.CrossConfig
import com.swordfish.radialgamepad.library.config.PrimaryDialConfig
import com.swordfish.radialgamepad.library.config.RadialGamePadConfig
import com.swordfish.radialgamepad.library.config.RadialGamePadTheme
import com.swordfish.radialgamepad.library.config.SecondaryDialConfig
import com.swordfish.radialgamepad.library.event.Event
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import org.libsdl.app.SDLActivity

/**
 * On-screen controls drawn over the game with RadialGamePad. They press the
 * keys of the game's default keyboard layout for player 1, so they work in the
 * menus and on the stages without setting up a controller: arrows steer,
 * accelerate and brake (and move through the menus), Space is the handbrake,
 * ] and [ change gear, C the camera, Enter and Escape confirm and pause.
 * They hide while a gamepad is played and come back on a touch.
 */
class TouchControls(private val activity: Activity, parent: ViewGroup) : InputManager.InputDeviceListener {
    private val scope: CoroutineScope = MainScope()
    private val inputManager = activity.getSystemService(Context.INPUT_SERVICE) as InputManager
    private val pressed = mutableSetOf<Int>()
    private val overlay = FrameLayout(activity)

    init {
        val theme = RadialGamePadTheme(
            normalColor = 0x50FFFFFF,
            pressedColor = 0xA0FFFFFF.toInt(),
            textColor = 0xE0FFFFFF.toInt(),
            backgroundColor = 0x28000000,
            lightColor = 0x30FFFFFF,
        )
        val left = RadialGamePad(leftConfig(theme), 8f, activity)
        val right = RadialGamePad(rightConfig(theme), 8f, activity)
        for ((pad, gravity) in listOf(left to (Gravity.BOTTOM or Gravity.START), right to (Gravity.BOTTOM or Gravity.END))) {
            overlay.addView(pad, FrameLayout.LayoutParams(0, 0, gravity))
            scope.launch { pad.events().collect(::handle) }
        }
        // Each pad takes about half the screen height, clear of display cutouts.
        overlay.addOnLayoutChangeListener { _, l, t, r, b, _, _, _, _ ->
            val size = (minOf(r - l, b - t) * 0.55f).toInt()
            for (pad in listOf(left, right)) {
                val params = pad.layoutParams
                if (params.width != size) {
                    params.width = size
                    params.height = size
                    pad.post { pad.layoutParams = params }
                }
            }
        }
        ViewCompat.setOnApplyWindowInsetsListener(overlay) { view, insets ->
            val cutout = insets.getInsets(WindowInsetsCompat.Type.displayCutout())
            view.setPadding(cutout.left, cutout.top, cutout.right, cutout.bottom)
            insets
        }
        parent.addView(overlay, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        inputManager.registerInputDeviceListener(this, Handler(Looper.getMainLooper()))
        updateVisibility()
    }

    private fun leftConfig(theme: RadialGamePadTheme) = RadialGamePadConfig(
        sockets = 12,
        primaryDial = PrimaryDialConfig.Cross(CrossConfig(id = CROSS, useDiagonals = false)),
        secondaryDials = listOf(
            SecondaryDialConfig.Empty(index = 1, spread = 1, scale = 1f, distance = 0f),
            button(index = 2, KeyEvent.KEYCODE_ESCAPE, R.drawable.ic_touch_pause, R.string.touch_pause),
            SecondaryDialConfig.Empty(index = 3, spread = 1, scale = 1f, distance = 0f),
        ),
        theme = theme,
    )

    private fun rightConfig(theme: RadialGamePadTheme) = RadialGamePadConfig(
        sockets = 12,
        // Counterclockwise from the right: Enter, accelerate, handbrake, brake.
        primaryDial = PrimaryDialConfig.PrimaryButtons(
            dials = listOf(
                ButtonConfig(KeyEvent.KEYCODE_ENTER, iconId = R.drawable.ic_touch_ok,
                    contentDescription = activity.getString(R.string.touch_ok)),
                ButtonConfig(KeyEvent.KEYCODE_DPAD_UP, iconId = R.drawable.ic_touch_accelerate,
                    contentDescription = activity.getString(R.string.touch_accelerate)),
                ButtonConfig(KeyEvent.KEYCODE_SPACE, iconId = R.drawable.ic_touch_handbrake,
                    contentDescription = activity.getString(R.string.touch_handbrake)),
                ButtonConfig(KeyEvent.KEYCODE_DPAD_DOWN, iconId = R.drawable.ic_touch_brake,
                    contentDescription = activity.getString(R.string.touch_brake)),
            ),
            allowMultiplePressesSingleFinger = false,
        ),
        secondaryDials = listOf(
            button(index = 3, KeyEvent.KEYCODE_RIGHT_BRACKET, R.drawable.ic_touch_gear_up, R.string.touch_gear_up),
            button(index = 4, KeyEvent.KEYCODE_LEFT_BRACKET, R.drawable.ic_touch_gear_down, R.string.touch_gear_down),
            button(index = 5, KeyEvent.KEYCODE_C, R.drawable.ic_touch_camera, R.string.touch_camera),
        ),
        theme = theme,
    )

    private fun button(index: Int, keyCode: Int, icon: Int, description: Int) = SecondaryDialConfig.SingleButton(
        index = index,
        scale = 1f,
        distance = 0f,
        buttonConfig = ButtonConfig(keyCode, iconId = icon, contentDescription = activity.getString(description)),
    )

    private fun handle(event: Event) {
        when (event) {
            is Event.Button -> set(event.id, event.action == KeyEvent.ACTION_DOWN)
            is Event.Direction -> {
                // The cross has no diagonals: one direction at a time, y down.
                set(KeyEvent.KEYCODE_DPAD_LEFT, event.xAxis < -0.5f)
                set(KeyEvent.KEYCODE_DPAD_RIGHT, event.xAxis > 0.5f)
                set(KEYCODE_CROSS_UP, event.yAxis < -0.5f)
                set(KEYCODE_CROSS_DOWN, event.yAxis > 0.5f)
            }
        }
    }

    // The cross and the right pad both press Up and Down; each keeps its own
    // state, and the key is down while either holds it.
    private fun set(id: Int, down: Boolean) {
        val wasDown = keyDown(keyOf(id))
        if (down) pressed += id else pressed -= id
        val isDown = keyDown(keyOf(id))
        if (isDown != wasDown) {
            if (isDown) SDLActivity.onNativeKeyDown(keyOf(id)) else SDLActivity.onNativeKeyUp(keyOf(id))
        }
    }

    private fun keyDown(keyCode: Int) = pressed.any { keyOf(it) == keyCode }

    private fun keyOf(id: Int) = when (id) {
        KEYCODE_CROSS_UP -> KeyEvent.KEYCODE_DPAD_UP
        KEYCODE_CROSS_DOWN -> KeyEvent.KEYCODE_DPAD_DOWN
        else -> id
    }

    /** Lets go of every key, when the game pauses or the controls hide. */
    fun releaseAll() {
        for (id in pressed.toList()) set(id, false)
    }

    // A controller, external or built into a handheld, has gamepad buttons.
    // Phones list other built-in devices with joystick sources too (the
    // OnePlus 12's touch panel), but those have no gamepad buttons.
    private fun isGamepad(device: InputDevice?) = device != null && !device.isVirtual &&
        device.supportsSource(InputDevice.SOURCE_GAMEPAD) && !device.supportsSource(InputDevice.SOURCE_TOUCHSCREEN)

    private fun show(visible: Boolean) {
        val show = visible || ControllerProfiles.showTouch(activity) || ControllerProfiles.touchOnly(activity)
        if (!show) releaseAll()
        overlay.isVisible = show
    }

    private fun updateVisibility() =
        show(inputManager.inputDeviceIds.none { isGamepad(inputManager.getInputDevice(it)) })

    /**
     * Follows what is being played, by the source of each event: a gamepad
     * button or stick hides the controls, a touch shows them again.
     */
    fun onInput(source: Int) {
        if ((source and InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) {
            if (!overlay.isVisible) show(true)
        } else if (overlay.isVisible && ((source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)) {
            show(false)
        }
    }

    override fun onInputDeviceAdded(deviceId: Int) = updateVisibility()
    override fun onInputDeviceRemoved(deviceId: Int) = updateVisibility()
    override fun onInputDeviceChanged(deviceId: Int) {}

    fun close() {
        releaseAll()
        inputManager.unregisterInputDeviceListener(this)
        scope.cancel()
        (overlay.parent as? ViewGroup)?.removeView(overlay)
    }

    private companion object {
        const val CROSS = -1
        // Ids of the cross's up and down, which press the same keys as the right pad.
        const val KEYCODE_CROSS_UP = -2
        const val KEYCODE_CROSS_DOWN = -3
    }
}

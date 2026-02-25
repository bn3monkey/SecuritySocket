package com.bn3monkey.securitysocketandroidtest

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith


/**
 * Instrumented test, which will execute on an Android device.
 *
 * See [testing documentation](http://d.android.com/tools/testing).
 */
@RunWith(AndroidJUnit4::class)
class SecuritySocketAndroidTest {
    init {
        System.loadLibrary("SecuritySocketAndroidTest")
    }

    external fun startSecuritySocketTest(cwd: String) : Int

    @Test
    fun main() {
        // Pass the app's internal files directory so cert files can be written
        // to a writable location on the Android device.
        val filesDir = InstrumentationRegistry.getInstrumentation().targetContext.filesDir.absolutePath
        startSecuritySocketTest(filesDir)
    }
}

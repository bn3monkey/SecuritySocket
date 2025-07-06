package com.bn3monkey.securitysocketandroidtest

import android.Manifest
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Rule
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

    external fun startSecuritySocketTest() : Int

    @Test
    fun main() {
        startSecuritySocketTest()
    }
}
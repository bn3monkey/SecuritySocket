package com.bn3monkey.securitysocket

import android.Manifest
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.rule.GrantPermissionRule

import org.junit.Test
import org.junit.runner.RunWith

import org.junit.Assert.*
import org.junit.Rule

/**
 * Instrumented test, which will execute on an Android device.
 *
 * See [testing documentation](http://d.android.com/tools/testing).
 */
@RunWith(AndroidJUnit4::class)
class ExampleInstrumentedTest {
    @get:Rule
    val runtimePermission: GrantPermissionRule = GrantPermissionRule.grant(Manifest.permission.INTERNET)

    @Test
    fun useAppContext() {
        // Context of the app under test.
        val appContext = InstrumentationRegistry.getInstrumentation().targetContext
        assertEquals("com.bn3monkey.securitysocket.test", appContext.packageName)
    }

    @Test
    fun tcpTest() {
        val nativeLib = NativeLib()
        nativeLib.tcpTest()
    }

    @Test
    fun tlsTest() {
        val nativeLib = NativeLib()
        nativeLib.tlsTest()
    }

    @Test
    fun tcpAsyncTest() {
        val nativeLib = NativeLib()
        nativeLib.tcpAsyncTest()
    }

    @Test
    fun tlsAsyncTest() {
        val nativeLib = NativeLib()
        nativeLib.tlsAsyncTest()
    }
}
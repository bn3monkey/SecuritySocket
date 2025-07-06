package com.bn3monkey.securitysocket

class NativeLib {

    /**
     * A native method that is implemented by the 'securitsocket' native library,
     * which is packaged with this application.
     */
    external fun tcpTest()
    external fun tlsTest()
    external fun tlsAsyncTest()
    external fun tcpAsyncTest()

    companion object {
        // Used to load the 'securitsocket' library on application startup.
        init {
            System.loadLibrary("securitysocket_test")
        }
    }
}
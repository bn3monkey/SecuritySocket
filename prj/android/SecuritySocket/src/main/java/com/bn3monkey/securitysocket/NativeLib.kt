package com.bn3monkey.securitysocket

class NativeLib {

    /**
     * A native method that is implemented by the 'securitysocket' native library,
     * which is packaged with this application.
     */
    external fun tcpTest()
    external fun tlsTest()

    companion object {
        // Used to load the 'securitysocket' library on application startup.
        init {
            System.loadLibrary("securitysocket_test")
        }
    }
}
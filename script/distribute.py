import json
import os
import shutil

def getPath() : 
    file_path = os.path.dirname(os.path.abspath(__file__)) + "\\path.json"
    with open(file_path, "r") as json_file : 
        data = json.load(json_file)
    ret = data["project_path"] + "\\" + data["relative_path"]
    return ret

def copyTo() :
    libraryPath = getPath()
    
    toIncludePath = os.path.join(libraryPath, "include")
    toBinaryPath = os.path.join(libraryPath, "lib")
    toWindowsBinaryPath = os.path.join(toBinaryPath, "windows")
    toAndroidBinaryPath = os.path.join(toBinaryPath, "android")

    projectPath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..") 
    fromIncludePath = os.path.join(projectPath, "src")
    fromWindowsBinaryPath = os.path.join(projectPath, "prj", "windows", "build", "SecuritySocket", "lib")
    fromAndroidBinaryPath = os.path.join(projectPath, "prj", "android", "SecuritySocket", "libs", "SecuritySocket", "lib")

    includeFileName = "SecuritySocket.hpp"
    fromIncludeFile = os.path.join(fromIncludePath, includeFileName)
    toIncludeFile = os.path.join(toIncludePath, includeFileName)

    if os.path.exists(toIncludePath) :
        shutil.rmtree(toIncludePath)
    os.makedirs(toIncludePath)

    if os.path.exists(toBinaryPath) : 
        shutil.rmtree(toBinaryPath)
    os.makedirs(toBinaryPath)
    # os.makedirs(toWindowsBinaryPath)
    # os.makedirs(toAndroidBinaryPath)

    shutil.copyfile(fromIncludeFile, toIncludeFile)
    shutil.copytree(fromWindowsBinaryPath ,toWindowsBinaryPath)
    shutil.copytree(fromAndroidBinaryPath ,toAndroidBinaryPath)

if __name__ == "__main__" : 
    copyTo()
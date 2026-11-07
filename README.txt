AirlineOS - Web Application

Requirements:
- Windows
- Visual Studio 2022 with Desktop development with C++
- vcpkg
- Crow
- SQLite3

Installation:
1. Install Visual Studio C++ workload.
2. Install vcpkg.
3. Install dependencies using:

   vcpkg install crow:x64-windows
   vcpkg install sqlite3:x64-windows

4. Open AirlineOS_Web.vcxproj in Visual Studio.
5. Select x64 and Debug.
6. Build and Run the project.

Open the browser and visit:
http://localhost:18080

Note:
Keep AirlineOS.db in the project directory.
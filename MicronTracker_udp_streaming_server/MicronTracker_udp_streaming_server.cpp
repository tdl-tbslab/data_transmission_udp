/******************************************************************************
Project: MicronTrackerHH.cpp
Authors: Jason Fong, Thomas Lehmann

Acknowledgements: Built upon code received from Meaghan Bowthorpe. Base code
created by an unknown author. Utilizes Micron Tracker code by Claron and
UDP data transmission class

Description: Acquires and sends MT data to a target PC via UDP.
Note: DO NOT CLOSE THE PROGRAM WHILE IT IS RUNNING WITHOUT FIRST HITTING A
KEY TO STOP THE TRACKING AND FREE DATA BUFFERS!!! Optionally, integrate
the closing/freeing code into whatever you use the motion tracker for.
******************************************************************************/

#include "stdafx.h"
#include "data_transmission.h"
#include "config.h"
#include <conio.h>
#include <math.h>
#include <time.h>
#include <MTC.h> // MTC.h need to be in the local directory or include path

// Macro to check for and report MTC usage errors.
#define MTC(func) {int r = func;  }; 

#ifdef WIN32
	int getMTHome(char *sMTHome, int size); // Forward declaration
#endif

float _X[2][2], _Y[2][2], _Z[2][2]; // First index is current/past position,
// second index is current/past time
float _Vx[2], _Vy[2], _Vz[2]; // First index is velocity, second is time


int _tmain(int argc, _TCHAR* argv[], char* envp[])
{
	printf("\n----------------------------------------------");
	printf("\n");
	printf("\nTelerobotic and Biorobotic Systems Lab");
	printf("\nMicronTracker UDP streaming server");
	printf("\n");
	printf("\n----------------------------------------------");
	printf("\n");

	// Connect to the available cameras, and report on what was found
	// The first camera is designated as the "current" camera - we will use
	// its coordinate space in reporting pose measurements.
	char MTHome[512];
	char calibrationDir[512];
	char markerDir[512];

	#ifdef WIN32
		if (getMTHome(MTHome, sizeof(MTHome)) < 0) {
			// No Environment
			printf("MTHome environment variable is not set!\n");
			//return 0;
		}
		else {
			sprintf_s(calibrationDir, "%s\\CalibrationFiles", MTHome);
			sprintf_s(markerDir, "%s\\Markers", MTHome);
		}
	#else  // Linux & Mac OSX
		sprintf(calibrationDir, "../CalibrationFiles");
		sprintf(markerDir, "../Markers");
	#endif

	// Create an instance of data_transmission class
	data_transmission transmission;

	Config config("MicronTracker_udp_streaming_server.cfg", envp);

	string tmp = config.pString("ip_local").c_str();
	char ip_local_scp[1024];
	strcpy_s(ip_local_scp, tmp.c_str());
	tmp = config.pString("ip_remote").c_str();
	char ip_remote_scp[1024];
	strcpy_s(ip_remote_scp, tmp.c_str());
	short port_remote_ss = (short)config.pInt("port_remote");
	short port_local_ss = (short)config.pInt("port_local");
	tmp = config.pString("name_marker").c_str();
	char marker_name_scp[1024];
	strcpy_s(marker_name_scp, tmp.c_str());

	printf("\nLocal IP address: %s:%d", ip_local_scp, port_local_ss);
	printf("\nRemote IP address: %s:%d", ip_remote_scp, port_remote_ss);
	
	int comm_error = 0;
	comm_error = transmission.init_transmission(ip_local_scp, port_local_ss,
		ip_remote_scp, port_remote_ss);

	if (comm_error) {
		printf("\nTransmission init failed with error code %d", comm_error);
		printf("\n");
		system("Pause");

		return -1;
	}

	// Back to camera initialization
	MTC(Cameras_AttachAvailableCameras(calibrationDir)); // Path to directory where the calibration files are
	if (Cameras_Count() < 1) {
		printf("\nNo camera found!");
		//return 0;
	}
	mtHandle CurrCamera, IdentifyingCamera;
	int		CurrCameraSerialNum;
	// Obtain a handle to the first/only camera in the array
	MTC(Cameras_ItemGet(0, &CurrCamera));
	// Obtain its serial number
	MTC(Camera_SerialNumberGet(CurrCamera, &CurrCameraSerialNum));
	printf("\nAttached %d camera(s). Curr camera is %d", Cameras_Count(), CurrCameraSerialNum);

	// Load the marker templates (with no validation).
	MTC(Markers_LoadTemplates(markerDir)); // Path to directory where the marker templates are
	printf("\nLoaded %d marker templates", Markers_TemplatesCount());

	// Create objects to receive the measurement results
	mtHandle IdentifiedMarkers = Collection_New();
	// TEMPORARY
	mtHandle t2m = Xform3D_New(); // tooltip to marker xform handle
	mtHandle m2c = Xform3D_New(); // marker to camera xform handle
	// TEMPORARY
	mtHandle PoseXf = Xform3D_New();

	//-=-=-=- DATA CAPTURE AND DEPOSITING -=-=-=-//
	int i = 0;
	//fprintf(stderr, "\nPress any key to quit.\n");
	Sleep(3000);

	int k = 0;
	printf("\nStreaming to %s:%d... \nPress any key to quit.", ip_remote_scp, port_remote_ss);
	while ((k == 0) && (!comm_error)){
		clock_t start = clock();
		k = _kbhit();
		i++;
		MTC(Cameras_GrabFrame(NULL)); // Grab a frame (all cameras together)
		MTC(Markers_ProcessFrame(NULL)); // Process the frame(s) to obtain measurements
		if (i < 40) continue; // The first 20 frames are auto-adjustment frames, and would be ignored here
		// Here, MTC internally maintains the measurement results.
		// Those results can be accessed until the next call to Markers_ProcessFrame, when they
		// are updated to reflect the next frame's content.
		// First, we will obtain the collection of the markers that were identified.
		MTC(Markers_IdentifiedMarkersGet(NULL, IdentifiedMarkers));
		// printf("%d: identified %d marker(s)\n", i, Collection_Count(IdentifiedMarkers));
		// Now we iterate on the identified markers (if any), and report their name and their pose
		for (int j = 1; j <= Collection_Count(IdentifiedMarkers); j++) {
			// Obtain the marker's handle, and use it to obtain the pose in the current camera's space
			// using our Xform3D object, PoseXf.
			mtHandle Marker = Collection_Int(IdentifiedMarkers, j);
			MTC(Marker_Marker2CameraXfGet(Marker, CurrCamera, PoseXf, &IdentifyingCamera));
			// We check the IdentifyingCamera output to find out if the pose is, indeed,
			// available in the current camera space. If IdentifyingCamera==0, the current camera's
			// coordinate space is not registered with any of the cameras which actually identified
			// the marker.
			if (IdentifyingCamera != 0) {
				char MarkerName[MT_MAX_STRING_LENGTH];
				double	Position[3], Angle[3];
				// We will also check and report any measurement hazard
				mtMeasurementHazardCode Hazard;
				MTC(Marker_NameGet(Marker, MarkerName, MT_MAX_STRING_LENGTH, 0));
				//printf("%s\n", MarkerName);

				// TEMPORARY
				// Get m2c
				MTC(Marker_Marker2CameraXfGet(Marker, CurrCamera, m2c, &IdentifyingCamera));
				// Get t2m
				MTC(Marker_Tooltip2MarkerXfGet(Marker, t2m));
				// Transform both to PoseXf
				MTC(Xform3D_Concatenate(t2m, m2c, PoseXf));
				// TEMPORARY

				// Get position
				MTC(Xform3D_ShiftGet(PoseXf, Position));
				// Here we obtain the rotation as a sequence of 3 angles. Often, it is more convenient
				// (and slightly more accurage) access the rotation as a 3x3 rotation matrix.
				MTC(Xform3D_RotAnglesDegsGet(PoseXf, &Angle[0], &Angle[1], &Angle[2]));
				MTC(Xform3D_HazardCodeGet(PoseXf, &Hazard));
				// Print the report
				// printf(">> %s at (%0.2f, %0.2f, %0.2f), rotation (degs): (%0.1f, %0.1f, %0.1f) %s\n",
				// MarkerName,
				_X[0][0] = Position[0];
				_Y[0][0] = Position[1];
				_Z[0][0] = Position[2];
				_X[0][1] = _Y[0][1] = _Z[0][1] = i;

				if (i < 40) {
					_Vx[0] = _Vy[0] = _Vz[0] = 0;
					_Vx[1] = _Vy[1] = _Vz[1] = i;
				}
				else if (i % 5 == 0) {
					_Vx[0] = 20 * (_X[0][0] - _X[1][0]) / (_X[0][1] - _X[1][1]);
					_Vy[0] = 20 * (_Y[0][0] - _Y[1][0]) / (_X[0][1] - _X[1][1]);
					_Vz[0] = 20 * (_Z[0][0] - _Z[1][0]) / (_X[0][1] - _X[1][1]);
					_Vx[1] = _Vy[1] = _Vz[1] = i;

					// Send position data if identified marker matches specified one
					if (!strcmp(MarkerName, marker_name_scp)) {
						const short len_data_ss = 3;
						double data[len_data_ss];
						data[0] = _X[0][0];
						data[1] = _Y[0][0];
						data[2] = _Z[0][0];
						comm_error = transmission.send(data, len_data_ss);

					}
				}
				// Update 'past' positions
				_X[1][0] = _X[0][0];
				_Y[1][0] = _Y[0][0];
				_Z[1][0] = _Z[0][0];
			}
		}
		_X[1][1] = _Y[1][1] = _Z[1][1] = i;

		clock_t end = clock();
		float seconds = (float)(end - start) / CLOCKS_PER_SEC;
		float sample_time = (1 / (double)20);
		if (seconds > sample_time) 
			printf("\nSample time > %2.4f s: %2.4f", sample_time, seconds);
	}

	//-=-=-=- CLEANUP -=-=-=-//
	// Free up all resources taken
	//sqlite3_close(db);

	Collection_Free(IdentifiedMarkers);
	Xform3D_Free(PoseXf);
	Cameras_Detach(); // Important - otherwise the cameras will continue capturing, locking up this process.

	printf("\n");
	system("Pause");

	return 0;
}

# ifdef WIN32
/********************************************************************/
int getMTHome(char *sMTHome, int size)
/********************************************************************/
{
	LONG err;
	HKEY key;
	char *mfile = "MTHome";
	DWORD value_type;
	DWORD value_size = size;


	/* Check registry key to determine log file name: */
	if ((err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0,
		KEY_QUERY_VALUE, &key)) != ERROR_SUCCESS) {
		return(-1);
	}

	if (RegQueryValueEx(key,
		mfile,
		0,	/* reserved */
		&value_type,
		(unsigned char*)sMTHome,
		&value_size) != ERROR_SUCCESS || value_size <= 1){
		/* size always >1 if exists ('\0' terminator) ? */
		return(-1);
	}
	return(0);
}
#endif


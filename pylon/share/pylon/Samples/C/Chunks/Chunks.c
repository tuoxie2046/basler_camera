/*
   Basler cameras provide "chunk features": The cameras can generate certain information about each image,
   e.g. frame counters, time stamps, and CRC checksums, which is appended to the image data as data "chunks".
   This sample illustrates how to enable chunk features, how to grab
   images, and how to process the appended data. When the camera is in chunk mode, it transfers data blocks
   that are partitioned into chunks. The first chunk is always the image data. When chunk features are enabled,
   the image data chunk is followed by chunks containing the information generated by the chunk features.

   This sample also demonstrates how to use software triggers. Two buffers are used. Once a buffer is filled,
   the acquisition of the next frame is triggered before processing the received buffer. This approach allows
   performing image acquisition while the processing of the previous image proceeds.
*/


#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>

#include <pylonc/PylonC.h>

#define CHECK( errc ) if ( GENAPI_E_OK != errc ) printErrorAndExit( errc )

/* This function can be used to wait for user input at the end of the sample program. */
void pressEnterToExit( void );

/* This method demonstrates how to retrieve the error message for the last failed function call. */
void printErrorAndExit( GENAPIC_RESULT errc );

/* Calculates the minimum and maximum gray value. */
void getMinMax( const unsigned char* pImg, int32_t width, int32_t height,
                unsigned char* pMin, unsigned char* pMax );

#define NUM_GRABS 20          /* Number of images to grab. */
#define NUM_BUFFERS 2         /* Number of buffers used for grabbing. */

int main( void )
{
    GENAPIC_RESULT              res;                        /* Return value of pylon methods. */
    size_t                      numDevices;                 /* Number of available devices. */
    PYLON_DEVICE_HANDLE         hDev;                       /* Handle for the pylon device. */
    PYLON_STREAMGRABBER_HANDLE  hGrabber;                   /* Handle for the pylon stream grabber. */
    PYLON_CHUNKPARSER_HANDLE    hChunkParser;               /* Handle for the parser extracting the chunk data. */
    PYLON_WAITOBJECT_HANDLE     hWait;                      /* Handle used for waiting for a grab to be finished. */
    size_t                      payloadSize;                /* Size of an image frame in bytes. */
    unsigned char*              buffers[NUM_BUFFERS];       /* Buffers used for grabbing. */
    PYLON_STREAMBUFFER_HANDLE   bufHandles[NUM_BUFFERS];    /* Handles for the buffers. */
    PylonGrabResult_t           grabResult;                 /* Stores the result of a grab operation. */
    int                         nGrabs;                     /* Counts the number of buffers grabbed. */
    size_t                      nStreams;                   /* The number of streams the device provides. */
    _Bool                       isAvail;                    /* Used for checking feature availability. */
    _Bool                       isReady;                    /* Used as an output parameter. */
    size_t                      i;                          /* Counter. */
    int                         ret = EXIT_FAILURE;         /* The return value. */
    const char*                 triggerSelectorValue = "FrameStart"; /* Preselect the trigger for image acquisition. */
    _Bool                       isAvailFrameStart;          /* Used for checking feature availability. */
    _Bool                       isAvailAcquisitionStart;    /* Used for checking feature availability. */

    hDev = PYLONC_INVALID_HANDLE;

    /* Before using any pylon methods, the pylon runtime must be initialized. */
    PylonInitialize();

    printf( "Enumerating devices ...\n" );

    /* Enumerate all camera devices. You must call
       PylonEnumerateDevices() before creating a device. */
    res = PylonEnumerateDevices( &numDevices );
    CHECK( res );
    if (0 == numDevices)
    {
        fprintf( stderr, "No devices found.\n" );
        /* Before exiting a program, PylonTerminate() should be called to release
           all pylon related resources. */
        PylonTerminate();
        pressEnterToExit();
        exit( EXIT_FAILURE );
    }

    printf( "Opening first device ...\n" );

    /* Get a handle for the first device found.  */
    res = PylonCreateDeviceByIndex( 0, &hDev );
    CHECK( res );

    /* Before using the device, it must be opened. Open it for setting
       parameters and for grabbing images. */
    res = PylonDeviceOpen( hDev, PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM );
    CHECK( res );


    /* Set the pixel format to Mono8 if available, where gray values will be output as 8 bit values for each pixel. */
    isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_PixelFormat_Mono8" );
    if (isAvail)
    {
        res = PylonDeviceFeatureFromString( hDev, "PixelFormat", "Mono8" );
        CHECK( res );
    }

    /* Check the available camera trigger mode(s) to select the appropriate one: acquisition start trigger mode (used by previous cameras;
    do not confuse with acquisition start command) or frame start trigger mode (equivalent to previous acquisition start trigger mode). */
    isAvailAcquisitionStart = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_TriggerSelector_AcquisitionStart" );
    isAvailFrameStart = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_TriggerSelector_FrameStart" );

    /* Check to see if the camera implements the acquisition start trigger mode only. */
    if (isAvailAcquisitionStart && !isAvailFrameStart)
    {
    /* ... Select the software trigger as the trigger source. */
        res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "AcquisitionStart" );
        CHECK( res );
        res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "On" );
        CHECK( res );
        triggerSelectorValue = "AcquisitionStart";
    }
    else
    {
        /* Camera may have the acquisition start trigger mode and the frame start trigger mode implemented.
        In this case, the acquisition trigger mode must be switched off. */
        if (isAvailAcquisitionStart)
        {
            res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "AcquisitionStart" );
            CHECK( res );
            res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "Off" );
            CHECK( res );
        }

        /* Disable frame burst start trigger if available. */
        isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_TriggerSelector_FrameBurstStart" );
        if (isAvail)
        {
            res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "FrameBurstStart" );
            CHECK( res );
            res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "Off" );
            CHECK( res );
        }

        /* To trigger each single frame by software or external hardware trigger: Enable the frame start trigger mode. */
        res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "FrameStart" );
        CHECK( res );
        res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "On" );
        CHECK( res );
    }

    /* Note: the trigger selector must be set to the appropriate trigger mode
    before setting the trigger source or issuing software triggers.
    Frame start trigger mode for newer cameras, acquisition start trigger mode for previous cameras. */
    PylonDeviceFeatureFromString( hDev, "TriggerSelector", triggerSelectorValue );

    /* Enable software triggering. */
    /* ... Select the software trigger as the trigger source. */
    res = PylonDeviceFeatureFromString( hDev, "TriggerSource", "Software" );
    CHECK( res );

    /* When using software triggering, the Continuous frame mode should be used. Once
       acquisition is started, the camera sends one image each time a software trigger is
       issued. */
    res = PylonDeviceFeatureFromString( hDev, "AcquisitionMode", "Continuous" );
    CHECK( res );

    /* For GigE cameras, we recommend increasing the packet size for better
       performance. When the network adapter supports jumbo frames, set the packet
       size to a value > 1500, e.g., to 8192. In this sample, we only set the packet size
       to 1500. */
    /* ... Check first to see if the GigE camera packet size parameter is supported and if it is writable. */
    isAvail = PylonDeviceFeatureIsWritable( hDev, "GevSCPSPacketSize" );
    if (isAvail)
    {
        /* ... The device supports the packet size feature. Set a value. */
        res = PylonDeviceSetIntegerFeature( hDev, "GevSCPSPacketSize", 1500 );
        CHECK( res );
    }

    /* Before enabling individual chunks, the chunk mode in general must be activated. */
    isAvail = PylonDeviceFeatureIsWritable( hDev, "ChunkModeActive" );
    if (!isAvail)
    {
        fprintf( stderr, "The device doesn't support the chunk mode.\n" );
        PylonTerminate();
        pressEnterToExit();
        exit( EXIT_FAILURE );
    }

    /* Activate the chunk mode. */
    res = PylonDeviceSetBooleanFeature( hDev, "ChunkModeActive", 1 );
    CHECK( res );

    /* Enable some individual chunks... */

    /* ... The frame counter chunk feature. */
    /* Is the chunk available? */
    isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_ChunkSelector_Framecounter" );
    if (isAvail)
    {
        /* Select the frame counter chunk feature. */
        res = PylonDeviceFeatureFromString( hDev, "ChunkSelector", "Framecounter" );
        CHECK( res );
        /* Can the chunk feature be activated? */
        isAvail = PylonDeviceFeatureIsWritable( hDev, "ChunkEnable" );
        if (isAvail)
        {
            /* Activate the chunk feature. */
            res = PylonDeviceSetBooleanFeature( hDev, "ChunkEnable", 1 );
            CHECK( res );
        }
    }
    else
    {
        /* try setting Standard feature naming convention (SFNC) FrameID name*/
        isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_ChunkSelector_FrameID" );
        if (isAvail)
        {
            /* Select the frame id chunk feature. */
            res = PylonDeviceFeatureFromString( hDev, "ChunkSelector", "FrameID" );
            CHECK( res );
            /* Can the chunk feature be activated? */
            isAvail = PylonDeviceFeatureIsWritable( hDev, "ChunkEnable" );
            if (isAvail)
            {
                /* Activate the chunk feature. */
                res = PylonDeviceSetBooleanFeature( hDev, "ChunkEnable", 1 );
                CHECK( res );
            }
        }
    }
    /* ... The CRC checksum chunk feature. */
    /*  Note: Enabling the CRC chunk feature is not a prerequisite for using
        chunks. Chunks can also be handled when the CRC feature is disabled. */
    isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_ChunkSelector_PayloadCRC16" );
    if (isAvail)
    {
        /* Select the CRC chunk feature. */
        res = PylonDeviceFeatureFromString( hDev, "ChunkSelector", "PayloadCRC16" );
        CHECK( res );
        /* Can the chunk feature be activated? */
        isAvail = PylonDeviceFeatureIsWritable( hDev, "ChunkEnable" );
        if (isAvail)
        {
            /* Activate the chunk feature. */
            res = PylonDeviceSetBooleanFeature( hDev, "ChunkEnable", 1 );
            CHECK( res );
        }
    }
    /* ... The Timestamp chunk feature. */
    isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_ChunkSelector_Timestamp" );
    if (isAvail)
    {
        /* Select the Timestamp chunk feature. */
        res = PylonDeviceFeatureFromString( hDev, "ChunkSelector", "Timestamp" );
        CHECK( res );
        /* Can the chunk feature be activated? */
        isAvail = PylonDeviceFeatureIsWritable( hDev, "ChunkEnable" );
        if (isAvail)
        {
            /* Activate the chunk feature. */
            res = PylonDeviceSetBooleanFeature( hDev, "ChunkEnable", 1 );
            CHECK( res );
        }
    }

    /* The data block containing the image chunk and the other chunks has a self-descriptive layout.
       A chunk parser is used to extract the appended chunk data from the grabbed image frame.
       Create a chunk parser. */
    res = PylonDeviceCreateChunkParser( hDev, &hChunkParser );
    CHECK( res );
    if (hChunkParser == PYLONC_INVALID_HANDLE)
    {
        /* The transport layer doesn't provide a chunk parser. */
        fprintf( stderr, "No chunk parser available.\n" );
        goto exit;
    }


    /* Image grabbing is done using a stream grabber.
       A device may be able to provide different streams. A separate stream grabber must
       be used for each stream. In this sample, we create a stream grabber for the default
       stream, i.e., the first stream ( index == 0 ).
    */

    /* Get the number of streams supported by the device and the transport layer. */
    res = PylonDeviceGetNumStreamGrabberChannels( hDev, &nStreams );
    CHECK( res );
    if (nStreams < 1)
    {
        fprintf( stderr, "The transport layer doesn't support image streams.\n" );
        PylonTerminate();
        pressEnterToExit();
        exit( EXIT_FAILURE );
    }

    /* Create and open a stream grabber for the first channel. */
    res = PylonDeviceGetStreamGrabber( hDev, 0, &hGrabber );
    CHECK( res );
    res = PylonStreamGrabberOpen( hGrabber );
    CHECK( res );

    /* Get a handle for the stream grabber's wait object. The wait object
       allows waiting for buffers to be filled with grabbed data. */
    res = PylonStreamGrabberGetWaitObject( hGrabber, &hWait );
    CHECK( res );

    /* Determine the required size of the grab buffer. Since activating chunks will increase the
       payload size and thus the required buffer size, do this after enabling the chunks. */
    res = PylonStreamGrabberGetPayloadSize( hDev, hGrabber, &payloadSize );
    CHECK( res );

    /* Allocate memory for grabbing.  */
    for (i = 0; i < NUM_BUFFERS; ++i)
    {
        buffers[i] = (unsigned char*) malloc( payloadSize );
        if (NULL == buffers[i])
        {
            fprintf( stderr, "Out of memory.\n" );
            PylonTerminate();
            pressEnterToExit();
            exit( EXIT_FAILURE );
        }
    }

    /* We must tell the stream grabber the number and size of the buffers
       we are using. */
    /* .. We will not use more than NUM_BUFFERS for grabbing. */
    res = PylonStreamGrabberSetMaxNumBuffer( hGrabber, NUM_BUFFERS );
    CHECK( res );
    /* .. We will not use buffers bigger than payloadSize bytes. */
    res = PylonStreamGrabberSetMaxBufferSize( hGrabber, payloadSize );
    CHECK( res );


    /*  Allocate the resources required for grabbing. After this, critical parameters
        that impact the payload size must not be changed until FinishGrab() is called. */
    res = PylonStreamGrabberPrepareGrab( hGrabber );
    CHECK( res );


    /* Before using the buffers for grabbing, they must be registered at
    the stream grabber. For each registered buffer, a buffer handle
    is returned. After registering, these handles are used instead of the
    raw pointers. */
    for (i = 0; i < NUM_BUFFERS; ++i)
    {
        res = PylonStreamGrabberRegisterBuffer( hGrabber, buffers[i], payloadSize, &bufHandles[i] );
        CHECK( res );
    }

    /* Feed the buffers into the stream grabber's input queue. For each buffer, the API
       allows passing in a pointer to additional context information. This pointer
       will be returned unchanged when the grab is finished. In our example, we use the index of the
       buffer as context information. */
    for (i = 0; i < NUM_BUFFERS; ++i)
    {
        res = PylonStreamGrabberQueueBuffer( hGrabber, bufHandles[i], (void*) i );
        CHECK( res );
    }

    /* Start the image acquisition engine. */
    res = PylonStreamGrabberStartStreamingIfMandatory( hGrabber );
    CHECK( res );

    /* Issue an acquisition start command. Because the trigger mode is enabled, issuing the acquisition start command
       itself will not trigger any image acquisitions. Issuing the start command simply prepares the camera to acquire images.
       Once the camera is prepared it will acquire one image for every trigger it receives. */
    res = PylonDeviceExecuteCommandFeature( hDev, "AcquisitionStart" );
    CHECK( res );


    /* Trigger the first image. */
    res = PylonDeviceExecuteCommandFeature( hDev, "TriggerSoftware" );
    CHECK( res );


    /* Grab NUM_GRABS images */
    nGrabs = 0;                         /* Counts the number of images grabbed. */
    while (nGrabs < NUM_GRABS)
    {
        size_t bufferIndex;              /* Index of the buffer. */
        unsigned char min, max;
        int32_t chunkWidth = 0; /* Data retrieved from the chunk parser. */
        int32_t chunkHeight = 0; /* Data retrieved from the chunk parser. */

        /* Wait for the next buffer to be filled. Wait up to 1000 ms. */
        res = PylonWaitObjectWait( hWait, 1000, &isReady );
        CHECK( res );
        if (!isReady)
        {
            /* Timeout occurred. */
            fprintf( stderr, "Grab timeout occurred.\n" );
            break; /* Stop grabbing. */
        }

        /* Since the wait operation was successful, the result of at least one grab
           operation is available. Retrieve it. */
        res = PylonStreamGrabberRetrieveResult( hGrabber, &grabResult, &isReady );
        CHECK( res );
        if (!isReady)
        {
            /* Oops. No grab result available? We should never have reached this point.
               Since the wait operation above returned without a timeout, a grab result
               should be available. */
            fprintf( stderr, "Failed to retrieve a grab result\n" );
            break;
        }

        nGrabs++;

        /* Trigger the next image. Since we passed more than one buffer to the stream grabber,
           the triggered image will be grabbed while the image processing is performed.  */
        res = PylonDeviceExecuteCommandFeature( hDev, "TriggerSoftware" );
        CHECK( res );


        /* Get the buffer index from the context information. */
        bufferIndex = (size_t) grabResult.Context;

        /* Check to see if the image was grabbed successfully. */
        if (grabResult.Status == Grabbed)
        {
            /*  The grab is successfull.  */

            unsigned char* buffer;        /* Pointer to the buffer attached to the grab result. */

            /* Get the buffer pointer from the result structure. Since we also got the buffer index,
            we could alternatively use buffers[bufferIndex]. */
            buffer = (unsigned char*) grabResult.pBuffer;

            printf( "Grabbed frame #%2d into buffer %2d.\n", nGrabs, (int) bufferIndex );

            /* Check to see if we really got image data plus chunk data. */
            if (grabResult.PayloadType != PayloadType_ChunkData)
            {
                fprintf( stderr, "Received a buffer not containing chunk data?\n" );
            }
            else
            {
                /* Process the chunk data. This is done by passing the grabbed image buffer
                   to the chunk parser. When the chunk parser has processed the buffer, the chunk
                   data can be accessed in the same manner as "normal" camera parameters.
                   The only exception is the CRC feature. There are dedicated functions for
                   checking the CRC checksum. */

                _Bool hasCRC;

                /* Let the parser extract the data. */
                res = PylonChunkParserAttachBuffer( hChunkParser, grabResult.pBuffer, (size_t) grabResult.PayloadSize );
                CHECK( res );

                /* Check the CRC. */
                res = PylonChunkParserHasCRC( hChunkParser, &hasCRC );
                CHECK( res );
                if (hasCRC)
                {
                    _Bool isOk;
                    res = PylonChunkParserCheckCRC( hChunkParser, &isOk );
                    CHECK( res );
                    printf( "Frame %d contains a CRC checksum. The checksum %s ok.\n", nGrabs, isOk ? "is" : "is not" );
                }
                {
                    const char *featureName = "ChunkFramecounter";
                    /* Retrieve the frame counter value. */
                    /* ... Check the availability. */
                    isAvail = PylonDeviceFeatureIsAvailable( hDev, featureName );
                    if (!isAvail)
                    {
                        /*if not available try using the SFNC feature FrameID*/
                        featureName = "ChunkFrameID";
                        isAvail = PylonDeviceFeatureIsAvailable( hDev, featureName );
                    }
                    printf( "Frame %d %s a frame counter chunk.\n", nGrabs, isAvail ? "contains" : "doesn't contain" );
                    if (isAvail)
                    {
                        /* ... Get the value. */
                        int64_t counter;
                        res = PylonDeviceGetIntegerFeature( hDev, featureName, &counter );
                        CHECK( res );
#if __STDC_VERSION__ >= 199901L || defined(__GNUC__)
                        printf( "Frame counter of frame %d: %lld.\n", nGrabs, (long long) counter );
#else
                        printf( "Frame counter of frame %d: %I64d.\n", nGrabs, counter );
#endif
                    }
                }
                /* Retrieve the frame width value. */
                /* ... Check the availability. */
                isAvail = PylonDeviceFeatureIsAvailable( hDev, "ChunkWidth" );
                printf( "Frame %d %s a width chunk.\n", nGrabs, isAvail ? "contains" : "doesn't contain" );
                if (isAvail)
                {
                    /* ... Get the value. */
                    res = PylonDeviceGetIntegerFeatureInt32( hDev, "ChunkWidth", &chunkWidth );
                    CHECK( res );
                    printf( "Width of frame %d: %d.\n", nGrabs, chunkWidth );
                }

                /* Retrieve the frame height value. */
                /* ... Check the availability. */
                isAvail = PylonDeviceFeatureIsAvailable( hDev, "ChunkHeight" );
                printf( "Frame %d %s a height chunk.\n", nGrabs, isAvail ? "contains" : "doesn't contain" );
                if (isAvail)
                {
                    /* ... Get the value. */
                    res = PylonDeviceGetIntegerFeatureInt32( hDev, "ChunkHeight", &chunkHeight );
                    CHECK( res );
                    printf( "Height of frame %d: %d.\n", nGrabs, chunkHeight );
                }
                /* Retrieve the frame timestamp value. */
                /* ... Check the availability. */
                isAvail = PylonDeviceFeatureIsAvailable( hDev, "ChunkTimestamp" );
                printf( "Frame %d %s a timestamp chunk.\n", nGrabs, isAvail ? "contains" : "doesn't contain" );
                if (isAvail)
                {
                    /* ... Get the value. */
                    int64_t timestamp;
                    res = PylonDeviceGetIntegerFeature( hDev, "ChunkTimestamp", &timestamp );
                    CHECK( res );
#if __STDC_VERSION__ >= 199901L || defined(__GNUC__)
                    printf( "Frame timestamp of frame %d: %lld.\n", nGrabs, (long long)timestamp );
#else
                    printf( "Frame timestamp of frame %d: %I64d.\n", nGrabs, timestamp );
#endif
                }
            }

            /* Perform the image processing. */
            getMinMax( buffer, chunkWidth, chunkHeight, &min, &max );
            printf( "Min. gray value  = %3u, Max. gray value = %3u\n", min, max );

            /* Before requeueing the buffer, you should detach it from the chunk parser. */
            res = PylonChunkParserDetachBuffer( hChunkParser );  /* The chunk data in the buffer is now no longer accessible. */
            CHECK( res );


        }
        else if (grabResult.Status == Failed)
        {
            fprintf( stderr, "Frame %d wasn't grabbed successfully.  Error code = 0x%08X\n",
                     nGrabs, grabResult.ErrorCode );
        }

        /* Once finished with the processing, requeue the buffer to be filled again. */
        res = PylonStreamGrabberQueueBuffer( hGrabber, grabResult.hBuffer, (void*) bufferIndex );
        CHECK( res );

    }

    /* Clean up. */

    /*  ... Stop the camera. */
    res = PylonDeviceExecuteCommandFeature( hDev, "AcquisitionStop" );
    CHECK( res );

    /* ... Stop the image acquisition engine. */
    res = PylonStreamGrabberStopStreamingIfMandatory( hGrabber );
    CHECK( res );

    /* ... We must issue a flush call to ensure that all pending buffers are put into the
    stream grabber's output queue. */
    res = PylonStreamGrabberFlushBuffersToOutput( hGrabber );
    CHECK( res );

    /* ... The buffers can now be retrieved from the stream grabber. */
    do
    {
        res = PylonStreamGrabberRetrieveResult( hGrabber, &grabResult, &isReady );
        CHECK( res );
    } while (isReady);

    /* ... When all buffers are retrieved from the stream grabber, they can be deregistered.
       After deregistering the buffers, it is safe to free the memory. */

    for (i = 0; i < NUM_BUFFERS; ++i)
    {
        res = PylonStreamGrabberDeregisterBuffer( hGrabber, bufHandles[i] );
        CHECK( res );
        free( buffers[i] );
    }

    /* ... Release grabbing related resources. */
    res = PylonStreamGrabberFinishGrab( hGrabber );
    CHECK( res );

    /* After calling PylonStreamGrabberFinishGrab(), parameters that impact the payload size (e.g.,
       the AOI width and height parameters) are unlocked and can be modified again. */

    /* ... Close the stream grabber. */
    res = PylonStreamGrabberClose( hGrabber );
    CHECK( res );

    /* ... Release the chunk parser. */
    res = PylonDeviceDestroyChunkParser( hDev, hChunkParser );
    CHECK( res );

    ret = EXIT_SUCCESS;

exit:

    /*  Disable the software trigger and the chunk mode. */
    if (hDev != PYLONC_INVALID_HANDLE)
    {
        res = PylonDeviceSetBooleanFeature( hDev, "ChunkEnable", 0 );
        CHECK( res );
        res = PylonDeviceSetBooleanFeature( hDev, "ChunkModeActive", 0 );
        CHECK( res );
        res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "Off" );
        CHECK( res );
    }


    /* ... Close and release the pylon device. The stream grabber becomes invalid
       after closing the pylon device. Don't call stream grabber related methods after
       closing or releasing the device. */
    res = PylonDeviceClose( hDev );
    CHECK( res );
    res = PylonDestroyDevice( hDev );
    CHECK( res );


    /* ... Shut down the pylon runtime system. Don't call any pylon method after
       calling PylonTerminate(). */
    PylonTerminate();
    pressEnterToExit();
    return ret;
}

/* This function demonstrates how to retrieve the error message for the last failed
   function call. */
void printErrorAndExit( GENAPIC_RESULT errc )
{
    char* errMsg;
    size_t length;

    /* Retrieve the error message.
    ... First find out how big the buffer must be, */
    GenApiGetLastErrorMessage( NULL, &length );
    errMsg = (char*) malloc( length );
    /* ... and retrieve the message. */
    GenApiGetLastErrorMessage( errMsg, &length );

    fprintf( stderr, "%s (%#08x).\n", errMsg, (unsigned int) errc );
    free( errMsg );

    /* Retrieve the more details about the error.
    ... First find out how big the buffer must be, */
    GenApiGetLastErrorDetail( NULL, &length );
    errMsg = (char*) malloc( length );
    /* ... and retrieve the message. */
    GenApiGetLastErrorDetail( errMsg, &length );

    fprintf( stderr, "%s\n", errMsg );
    free( errMsg );

    PylonTerminate();  /* Releases all pylon resources. */
    pressEnterToExit();
    exit( EXIT_FAILURE );
}

/* Simple "image processing" function returning the minimum and maximum gray
   value of an image with 8 bit gray values. */
void getMinMax( const unsigned char* pImg, int32_t width, int32_t height,
                unsigned char* pMin, unsigned char* pMax )
{
    unsigned char min = 255;
    unsigned char max = 0;
    unsigned char val;
    const unsigned char* p;

    for (p = pImg; p < pImg + width * height; p++)
    {
        val = *p;
        if (val > max)
            max = val;
        if (val < min)
            min = val;
    }
    *pMin = min;
    *pMax = max;
}

/* This function can be used to wait for user input at the end of the sample program. */
void pressEnterToExit( void )
{
    fprintf( stderr, "\nPress enter to exit.\n" );
    while (getchar() != '\n');
}


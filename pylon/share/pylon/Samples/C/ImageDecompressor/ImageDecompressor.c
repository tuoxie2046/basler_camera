/*

This sample illustrates how to configure the Compression Beyond feature
on an ace 2 Pro camera.
This allows the camera to send compressed image data to the host computer.
The compressed image data can be decompressed on the host side using the
PylonImageDecompressor set of functions.

Using the image compression feature reduces the amount of data transferred,
which in turn can result in increasing the resulting frame rate.
When compression is used, the camera sends the compressed image data as
chunk data. You can use the PylonImageDecompressorDecompressImage function
to convert the the compressed chunk data into the pixel data.

Note: Not all camera models support image compression.
*/

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>

#include <genapic/GenApiC.h>
#include <pylonc/PylonC.h>

/* Simple error handling. */
#define CHECK( errc ) if ( GENAPI_E_OK != errc ) printErrorAndExit( errc )

/* This function can be used to wait for user input at the end of the sample program. */
void pressEnterToExit( void );

/* This method demonstrates how to retrieve the error message
   for the last failed function call. */
void printErrorAndExit( GENAPIC_RESULT errc );

/* Calculating the minimum and maximum gray value of an image buffer. */
void getMinMax( const unsigned char* pImg, int32_t width, int32_t height,
                unsigned char* pMin, unsigned char* pMax );

/* Configure the camera to use compression. */
GENAPIC_RESULT configureCompression( PYLON_DEVICE_HANDLE hDev, double ratio );

int main( void )
{
    GENAPIC_RESULT          res;           /* Return value of pylon methods. */
    size_t                  numDevices;    /* Number of available devices. */
    PYLON_DEVICE_HANDLE     hDev;          /* Handle for the pylon device. */
    NODEMAP_HANDLE          hDeviceNodeMap;/* Handle for nodemap. */
    NODE_HANDLE             hDescNode;     /* Handle for the register node. */
    const int               numGrabs = 3;  /* Number of images to grab. */
    size_t                  payloadSize = 0; /* Size of the compressed image data in bytes. */
    size_t                  decompressedImageSize = 0; /* Size of a decompressed image frame in bytes. */
    size_t                  descriptorSize = 0; /* Size of the compression descriptor. */
    unsigned char*          descriptorBuf = NULL; /* Buffer used to store compression descriptor. */
    unsigned char*          imgBufCompressed = NULL; /* Buffer used to store compressed image data. */
    unsigned char*          imgBuf = NULL; /* Buffer used to store decompressed image data. */
    _Bool                   isAvail;
    int                     i;

    /* Before using any pylon methods, the pylon runtime must be initialized. */
    PylonInitialize();

    /* Enumerate all camera devices. You must call
    PylonEnumerateDevices() before creating a device! */
    res = PylonEnumerateDevices( &numDevices );
    CHECK( res );
    if (0 == numDevices)
    {
        fprintf( stderr, "No devices found!\n" );
        /* Before exiting a program, PylonTerminate() must be called to release
           all pylon-related resources. */
        PylonTerminate();
        pressEnterToExit();
        exit( EXIT_FAILURE );
    }

    /* Get a handle for the first device found.  */
    res = PylonCreateDeviceByIndex( 0, &hDev );
    CHECK( res );

    /* The device must be opened in order to configure parameters and grab images. */
    res = PylonDeviceOpen( hDev, PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM );
    CHECK( res );

    /* Check whether the camera supports the compression feature. */
    isAvail = PylonDeviceFeatureIsAvailable( hDev, "ImageCompressionMode" );
    if (isAvail)
    {
        PYLON_IMAGE_DECOMPRESSOR_HANDLE hDecompressor = PYLONC_INVALID_HANDLE;
        double fps = 0.;

        /* Set the pixel format to Mono8, if available, where gray values will be output as 8-bit values for each pixel. */
        isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_PixelFormat_Mono8" );
        if (isAvail)
        {
            res = PylonDeviceFeatureFromString( hDev, "PixelFormat", "Mono8" );
            CHECK( res );
        }

        /* Disable acquisition start trigger, if available. */
        isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_TriggerSelector_AcquisitionStart" );
        if (isAvail)
        {
            res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "AcquisitionStart" );
            CHECK( res );
            res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "Off" );
            CHECK( res );
        }

        /* Disable frame burst start trigger, if available. */
        isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_TriggerSelector_FrameBurstStart" );
        if (isAvail)
        {
            res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "FrameBurstStart" );
            CHECK( res );
            res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "Off" );
            CHECK( res );
        }

        /* Disable frame start trigger, if available */
        isAvail = PylonDeviceFeatureIsAvailable( hDev, "EnumEntry_TriggerSelector_FrameStart" );
        if (isAvail)
        {
            res = PylonDeviceFeatureFromString( hDev, "TriggerSelector", "FrameStart" );
            CHECK( res );
            res = PylonDeviceFeatureFromString( hDev, "TriggerMode", "Off" );
            CHECK( res );
        }

        /* For GigE cameras, Basler recommends increasing the packet size for better performance.
           If the network adapter supports jumbo frames,
           set the packet size to a value > 1500, e.g., to 8192.
           In this sample, we only set the packet size to 1500. */
        isAvail = PylonDeviceFeatureIsWritable( hDev, "GevSCPSPacketSize" );
        if (isAvail)
        {
            /* ... The device supports the packet size feature. Set a value. */
            res = PylonDeviceSetIntegerFeature( hDev, "GevSCPSPacketSize", 1500 );
            CHECK( res );
        }

        /* Turn off the compression feature to read the FPS without compression. */
        res = configureCompression( hDev, 0.0 );
        CHECK( res );

        res = PylonDeviceGetFloatFeature(hDev, "ResultingFrameRate", &fps);
        CHECK( res );
        printf( "Expected frames per second without compression: %.2f\n", fps );


        /* Configure the camera for lossless compression using a compression ratio of 75 %.
           You can adjust this value as required. */
        res = configureCompression( hDev, 75.0 );
        CHECK( res );

        /* Show the FPS with compression turned on. */
        res = PylonDeviceGetFloatFeature( hDev, "ResultingFrameRate", &fps );
        CHECK( res );
        printf( "Expected frames per second using compression: %.2f\n", fps );


        /* Determine the required size of the grab buffer. */
        {
            PYLON_STREAMGRABBER_HANDLE  hGrabber;
            int64_t value;

            /* Temporary create and open a stream grabber for the first channel. */
            res = PylonDeviceGetStreamGrabber( hDev, 0, &hGrabber );
            CHECK( res );
            res = PylonStreamGrabberOpen( hGrabber );
            CHECK( res );

            /* With compression turned on, the returned payload size is the 
               estimated maximum size of the compressed image data.
               The actual size of image data transferred may be smaller, depending on image contents. */
            res = PylonStreamGrabberGetPayloadSize( hDev, hGrabber, &payloadSize );
            CHECK( res );

            res = PylonStreamGrabberClose( hGrabber );
            CHECK( res );

            /* Get the size of the decompressed image data. */
            res = PylonDeviceGetIntegerFeature( hDev, "BslImageCompressionBCBDecompressedImageSize", &value );
            CHECK( res );
            decompressedImageSize = (size_t) value;
        }

        /* Create the decompressor. */
        res = PylonImageDecompressorCreate( &hDecompressor );
        CHECK( res );

        /* Get size of the compression descriptor required to decompress the image data from a register node.
           There is no pylon convenience function to read register nodes, so we use genapi functions. */
        res = PylonDeviceGetNodeMap( hDev, &hDeviceNodeMap );
        CHECK( res );
        res = GenApiNodeMapGetNode( hDeviceNodeMap, "BslImageCompressionBCBDescriptor", &hDescNode );
        CHECK( res );

        /* Get the length of the register node. */
        res = GenApiRegisterGetLength( hDescNode, &descriptorSize );
        CHECK( res );

        /* Allocate memory for the descriptor.
           The descriptor is small, allocation should not fail. */
        descriptorBuf = (unsigned char*) malloc( descriptorSize );

        /* Read the compression descriptor from the camera. */
        res = GenApiRegisterGetValue( hDescNode, descriptorBuf, &descriptorSize );
        CHECK( res );

        /* Set the descriptor in the decompressor. */
        res = PylonImageDecompressorSetCompressionDescriptor( hDecompressor, descriptorBuf, descriptorSize );
        CHECK( res );

        /* The decompresser has stored a copy of the descriptor. We can now free the descriptor. */
        free( descriptorBuf );
        descriptorBuf = NULL;

        /* Allocate memory. */
        imgBufCompressed = (unsigned char*) malloc( payloadSize );
        imgBuf = (unsigned char*) malloc( decompressedImageSize );

        if (NULL == imgBufCompressed || NULL == imgBuf)
        {
            fprintf( stderr, "Out of memory.\n" );
            PylonTerminate();
            pressEnterToExit();
            exit( EXIT_FAILURE );
        }

        /* Grab some images in a loop. */
        for (i = 0; i < numGrabs; ++i)
        {
            PylonGrabResult_t grabResult = { 0 };
            PylonCompressionInfo_t compInfo = { 0 };
            _Bool bufferReady;

            /* Grab a single frame from stream channel 0.
               The camera is set to single frame acquisition mode.
               Wait up to 2000 ms for the image to be grabbed. */
            res = PylonDeviceGrabSingleFrame( hDev, 0, imgBufCompressed, payloadSize,
                                              &grabResult, &bufferReady, 2000 );
            if (GENAPI_E_OK == res && !bufferReady)
            {
                /* Timeout occurred. */
                printf( "Frame %d: timeout\n", i + 1 );
            }
            CHECK( res );

            /* Check to see if the data has been received successfully. */
            if (grabResult.Status == Grabbed && grabResult.PayloadType == PayloadType_ChunkData)
            {
                /* Retrieve infos about the compressed image data (sent as chunk data). */
                res = PylonImageDecompressorGetCompressionInfo( grabResult.pBuffer, (size_t)grabResult.PayloadSize, &compInfo );
                CHECK( res );

                /* Does the returned chunk payload contain a successfully compressed image? */
                if (compInfo.HasCompressedImage && compInfo.CompressionStatus == CompressionStatus_Ok)
                {
                    unsigned char min, max;
                    double ratio = 0;

                    /* Decompress the chunk data into imgBuf. */
                    res = PylonImageDecompressorDecompressImage( hDecompressor, imgBuf, &decompressedImageSize, grabResult.pBuffer, (size_t) grabResult.PayloadSize, NULL );
                    CHECK( res );

                    /* Use the actual size of the returned data. */
                    ratio = (double)grabResult.PayloadSize / (double) compInfo.DecompressedPayloadSize;

                    /* Compressed images are sent as chunk data (PayloadType_ChunkData).
                       Most members of grabResult don't contain valid data.
                       This information can be retrieved from compInfo. */

                    /* Success. Now you can perform image processing on the image. */
                    getMinMax( imgBuf, compInfo.SizeX, compInfo.SizeY, &min, &max );
                    printf( "Grabbed frame #%2d: Compression Ratio: %.2f%%, Min. gray value = %3u, Max. gray value = %3u\n", i + 1, ratio, min, max );

#ifdef GENAPIC_WIN_BUILD
                    /* Display image. */
                    res = PylonImageWindowDisplayImage( 0, imgBuf, decompressedImageSize, compInfo.PixelType, compInfo.SizeX, compInfo.SizeY, compInfo.PaddingX, ImageOrientation_TopDown );
                    CHECK( res );
#endif
                }
                else
                {
                    printf( "Grabbed frame #%2d: Camera could not compress image. CompressionStatus = %d\n", i + 1, compInfo.CompressionStatus );
                }
            }
            else if (grabResult.Status == Failed)
            {
                fprintf( stderr, "Frame %d wasn't grabbed successfully.  Error code = 0x%08X\n",
                         i + 1, grabResult.ErrorCode );
            }
        }

        /* Free memory. */
        free( imgBuf );
        free( imgBufCompressed );

        /* Free all resources allocated by the decompressor. */
        res = PylonImageDecompressorDestroy( hDecompressor );
        CHECK( res );

        /* Turn off compression. */
        res = configureCompression( hDev, 0.0 );
        CHECK( res );
    }
    else
    {
        printf( "Camera does not support compression.\n");
    }

    /* Clean up. Close and release the pylon device. */

    res = PylonDeviceClose( hDev );
    CHECK( res );
    res = PylonDestroyDevice( hDev );
    CHECK( res );

    pressEnterToExit();

    /* Shut down the pylon runtime system. Don't call any pylon method after
       calling PylonTerminate(). */
    PylonTerminate();

    return EXIT_SUCCESS;
}

/* This function demonstrates how to retrieve the error message for the last failed
   function call. */
void printErrorAndExit( GENAPIC_RESULT errc )
{
    char* errMsg;
    size_t length;

    /* Retrieve the error message.
    ... Find out first how big the buffer must be, */
    GenApiGetLastErrorMessage( NULL, &length );
    errMsg = (char*) malloc( length );
    /* ... and retrieve the message. */
    GenApiGetLastErrorMessage( errMsg, &length );

    fprintf( stderr, "%s (%#08x).\n", errMsg, (unsigned int) errc );
    free( errMsg );

    /* Retrieve more details about the error.
    ... Find out first how big the buffer must be, */
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
   value of an 8-bit gray value image. */
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

/* This function configures the camera to use compression.
   The ratio parameter will set the BslImageCompressionRatio feature on the camera.
   If you pass a ratio of 0 compression will be turned off. */
GENAPIC_RESULT configureCompression( PYLON_DEVICE_HANDLE hDev, double ratio )
{
    GENAPIC_RESULT res = GENAPI_E_OK;

    if (ratio == 0)
    {
        /* Turn compression off. */
        res = PylonDeviceFeatureFromString( hDev, "ImageCompressionMode", "Off" );
    }
    else
    {
        /* Turn the compression feature on. */
        res = PylonDeviceFeatureFromString( hDev, "ImageCompressionMode", "BaslerCompressionBeyond" );
        if (res != GENAPI_E_OK) return res;

        /* We're using lossless compression, so image quality is not affected. */
        res = PylonDeviceFeatureFromString( hDev, "ImageCompressionRateOption", "Lossless" );
        if (res != GENAPI_E_OK) return res;

        /* In this sample we use a compression ratio of 75 %.
           You can adjust this value depending on your image contents and the required frame-rate.
           In addition, you can also configure the camera for lossy compression. This is not demonstrated in this sample.
           For more information, refer to the Basler Product Documentation.*/
        res = PylonDeviceSetFloatFeature( hDev, "BslImageCompressionRatio", ratio );
        if (res != GENAPI_E_OK) return res;
    }
    
    return res;
}

/* This function can be used to wait for user input at the end of the sample program. */
void pressEnterToExit( void )
{
    fprintf( stderr, "\nPress enter to exit.\n" );
    while (getchar() != '\n');
}


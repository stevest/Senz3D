/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2011-2012 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <wchar.h>
#include <vector>
#include <tchar.h>

#include "pxcsession.h"
#include "pxcsmartptr.h"
#include "pxccapture.h"
#include "util_render.h"
#include "util_capture_file.h"
#include "util_cmdline.h"

#define MAXLINE 1024

int wmain(int argc, WCHAR* argv[]) {
  PXCSmartPtr<PXCSession> session;
	pxcStatus sts=PXCSession_Create(&session);
	if (sts<PXC_STATUS_NO_ERROR) {
		wprintf_s(L"Failed to create an SDK session\n");
		return 3;
	}

	UtilCmdLine cmdl(session);
	if (!cmdl.Parse(L"-listio-nframes-sdname-csize-dsize-file-record",argc,argv)) return 3;

	UtilCaptureFile capture(session, cmdl.m_recordedFile, cmdl.m_bRecord);
	/* set source device search critieria */
	for (std::list<std::pair<PXCSizeU32,pxcU32>>::iterator itrc=cmdl.m_csize.begin();itrc!=cmdl.m_csize.end();itrc++)
		capture.SetFilter(PXCImage::IMAGE_TYPE_COLOR,itrc->first,itrc->second);
	for (std::list<std::pair<PXCSizeU32,pxcU32>>::iterator itrd=cmdl.m_dsize.begin();itrd!=cmdl.m_dsize.end();itrd++)
		capture.SetFilter(PXCImage::IMAGE_TYPE_DEPTH,itrd->first,itrd->second);

	PXCCapture::VideoStream::DataDesc request; 
	memset(&request, 0, sizeof(request));
	request.streams[0].format=PXCImage::COLOR_FORMAT_RGB32; 
	request.streams[1].format=PXCImage::COLOR_FORMAT_DEPTH;
	sts = capture.LocateStreams (&request); 
	if (sts<PXC_STATUS_NO_ERROR) {
		// let's search for color only
		request.streams[1].format=0;
		sts = capture.LocateStreams(&request); 
		if (sts<PXC_STATUS_NO_ERROR) {
			wprintf_s(L"Failed to locate any video stream(s)\n");
			return 1;
		}
	}

	std::vector<UtilRender*> renders;
	std::vector<PXCCapture::VideoStream*> streams; 
	for (int idx=0;;idx++) {
		PXCCapture::VideoStream *stream_v=capture.QueryVideoStream(idx);
		if (!stream_v) break;

		PXCCapture::VideoStream::ProfileInfo pinfo;
		stream_v->QueryProfile(&pinfo);
		WCHAR stream_name[256];
		switch (pinfo.imageInfo.format&PXCImage::IMAGE_TYPE_MASK) {
		case PXCImage::IMAGE_TYPE_COLOR: 
			swprintf_s<256>(stream_name, L"Stream#%d (Color) %dx%d", idx, pinfo.imageInfo.width, pinfo.imageInfo.height);
			break;
		case PXCImage::IMAGE_TYPE_DEPTH: 
			swprintf_s<256>(stream_name, L"Stream#%d (Depth) %dx%d", idx, pinfo.imageInfo.width, pinfo.imageInfo.height);
			break;
		}
		renders.push_back(new UtilRender(stream_name));
		streams.push_back(stream_v);
	}

	int i, nwindows=0, nstreams = (int)renders.size();
	PXCSmartSPArray sps(nstreams);
	PXCSmartArray<PXCImage> image(nstreams);

	for (i=0;i<nstreams;i++) { 
		sts=streams[i]->ReadStreamAsync (&image[i], &sps[i]); 
		if (sts>=PXC_STATUS_NO_ERROR) nwindows++;
	}

	//PXCImage *imageD, *imageC;
	PXCImage::ImageData dataD, dataC;
	PXCImage::ImageInfo infoD,infoC;
	FILE* fid;
	bool gotDepth=0;
	bool gotColor=0;
	char nameRColor[MAXLINE];
	char nameGColor[MAXLINE];
	char nameBColor[MAXLINE];
	char nameDepth[MAXLINE];
	char nameConfidence[MAXLINE];
	//char nameUV[MAXLINE];
	int interationNo = 1;

	for (int k=0; k<(int)30 && nwindows>0; k++) {
		pxcU32 sidx=0;
		if (sps.SynchronizeEx(&sidx)<PXC_STATUS_NO_ERROR) break;

		// loop through all active streams as SynchronizeEx only returns the first active
		for (int j=(int)sidx;j<nstreams;j++) {
			if (!sps[j]) continue;
			if (sps[j]->Synchronize(0)<PXC_STATUS_NO_ERROR) continue;
			sps.ReleaseRef(j);

			bool active=renders[j]->RenderFrame(image[j]);
			if(j==0){gotColor = 1;}
			if(j==1){gotDepth = 1;}
			if (active) {
				sts=streams[j]->ReadStreamAsync(image.ReleaseRef(j), &sps[j]);
				if (sts<PXC_STATUS_NO_ERROR) sps[j]=0, active=false;


			}
			if (active) continue;



			// we have to close the window here, or the window would not be responsive (No MessageLoop)
			--nwindows;
			renders[j]->Release(); 
			renders[j]=0;
		}
		if(gotColor && gotDepth){

			//Reset names:
			memset(nameBColor,0,MAXLINE);
			memset(nameGColor,0,MAXLINE);
			memset(nameRColor,0,MAXLINE);
			memset(nameDepth,0,MAXLINE);
			memset(nameConfidence,0,MAXLINE);
			//Assign Names:
			sprintf(nameBColor,"ColorB_%d.txt",k);
			sprintf(nameGColor,"ColorG_%d.txt",k);
			sprintf(nameRColor,"ColorR_%d.txt",k);
			sprintf(nameDepth,"Depth_%d.txt",k);
			sprintf(nameConfidence,"IR_%d.txt",k);
			//Dump streams:
			unsigned char *uptr;
			image[0]->AcquireAccess(PXCImage::ACCESS_READ,&dataC);
			image[0]->QueryInfo(&infoC);

			fid = fopen(nameBColor,"w");
			int ctr=0;
			uptr = (unsigned char*)dataC.planes[0];
			for(int ii=0;ii<infoC.height;ii++){
				for(int jj=0;jj<infoC.width;jj++){
					fprintf(fid,"%d ",*(uptr+ctr*3));
					ctr++;
				}
				fprintf(fid,"\n");
			}
			fclose(fid);
			fid = fopen(nameGColor,"w");
			ctr=0;
			uptr = (unsigned char*)dataC.planes[0];
			for(int ii=0;ii<infoC.height;ii++){
				for(int jj=0;jj<infoC.width;jj++){
					fprintf(fid,"%d ",*(uptr+1+ctr*3));
					ctr++;
				}
				fprintf(fid,"\n");
			}
			fclose(fid);
			fid = fopen(nameRColor,"w");
			ctr=0;
			uptr = (unsigned char*)dataC.planes[0];
			for(int ii=0;ii<infoC.height;ii++){
				for(int jj=0;jj<infoC.width;jj++){
					fprintf(fid,"%d ",*(uptr+2+ctr*3));
					ctr++;
				}
				fprintf(fid,"\n");
			}
			fclose(fid);
			image[0]->ReleaseAccess(&dataC);
			//gotColor = 1;

			__int16 *iptr;
			image[1]->AcquireAccess(PXCImage::ACCESS_READ,&dataD);
			image[1]->QueryInfo(&infoD);

			fid = fopen(nameDepth,"w");
			ctr=0;
			iptr = (__int16*)dataD.planes[0];
			for(int ii=0;ii<infoD.height;ii++){
				for(int jj=0;jj<infoD.width;jj++){
					fprintf(fid,"%d ",*(iptr+ctr));
					ctr++;
				}
				fprintf(fid,"\n");
			}
			fclose(fid);
			fid = fopen(nameConfidence,"w");
			ctr=0;
			iptr = (__int16*)dataD.planes[1];
			for(int ii=0;ii<infoD.height;ii++){
				for(int jj=0;jj<infoD.width;jj++){
					fprintf(fid,"%d ",*(iptr+ctr));
					ctr++;
				}
				fprintf(fid,"\n");
			}
			fclose(fid);
			/*fid = fopen("UV.txt","w");
			ctr=0;
			float *fptr = (float*)dataD.planes[2];
			for(int ii=0;ii<infoD.height;ii++){
			for(int jj=0;jj<infoD.width;jj++){
			fprintf(fid,"%f ",*(fptr+ctr));
			ctr++;
			}
			fprintf(fid,"\n");
			}
			fclose(fid);*/
			image[1]->ReleaseAccess(&dataD);
			//gotDepth = 1;

			gotDepth=0;
			gotColor=0;
			//END dumping streams.
		} else {
			gotDepth=0;
			gotColor=0;
		}

		//Wait for user input:
		printf("Grabbed frames #%d",k);
		getchar();
	}
	sps.SynchronizeEx();

	// destroy resources
	for (i=0;i<nstreams;i++)
		if (renders[i]) renders[i]->Release();

	return 0;
}

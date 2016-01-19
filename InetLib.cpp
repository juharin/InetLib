// InetLib.cpp
#include "pch.h"
#include "InetLib.h"
#include <string>
#include <collection.h>

#pragma comment(lib, "wininet.lib")

using namespace InetLib;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::System::Threading;

//#define ASYNC

FtpClient::FtpClient() {}

FtpClient::FtpClient(String^ userAgent)
{
    _userAgent = userAgent->Data();
    _readBufferSize = 0;
    _writeBufferSize = 0;
}

FtpClient::~FtpClient()
{
    Close();
}

int FtpClient::Connect(String^ host)
{
    int error = NOERROR;

    _transferFlags = FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_PASSIVE | INTERNET_FLAG_RELOAD;
    _inetSession = InternetOpen(_userAgent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (_inetSession != NULL)
    {
        _mode = INTERNET_SERVICE_FTP;
        _host = host;
        _port = INTERNET_DEFAULT_FTP_PORT;
        error = InitializeSession();
    }
    else
    {
        error = GetLastError();
    }

    return error;
}

int FtpClient::InitializeSession()
{
    int error = NOERROR;

    _inetFileInfo = InternetConnect(_inetSession, _host->Data(), _port, _userName->Data(), _password->Data(), _mode, _transferFlags, 0);
    if (!_inetFileInfo)
    {
        error = GetLastError();
    }

    return error;
}

void FtpClient::CloseSession()
{
    InternetCloseHandle(_inetFileInfo);
    InternetCloseHandle(_inetSession);
}

bool FtpClient::Close()
{
    bool status = TRUE;

    if (_inetResource != NULL)
    {
        status = InternetCloseHandle(_inetResource);
    }

    if (_inetFileInfo != NULL)
    {
        status = InternetCloseHandle(_inetFileInfo);
    }

    if (_inetSession != NULL)
    {
        status = InternetCloseHandle(_inetSession);
    }

    return status;
}

bool FtpClient::UploadFile(String^ localFilePath, String^ ftpFilePath)
{
    bool status = true;

    CREATEFILE2_EXTENDED_PARAMETERS extParams;
    extParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
    extParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    extParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    extParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
    extParams.lpSecurityAttributes = NULL;
    extParams.hTemplateFile = NULL;

    HANDLE localFileHandle = CreateFile2(localFilePath->Data(), 
                                         GENERIC_READ, 
                                         FILE_SHARE_READ, 
                                         OPEN_EXISTING, 
                                         &extParams);
    if (localFileHandle == INVALID_HANDLE_VALUE)
    {
        TransferStatusText = "Cannot open the file " + localFilePath + ", " + GetLastErrorMsg();
        return false;
    }

    LARGE_INTEGER fileSizeStructure;
	
	
	//if (!GetFileSizeEx(localFileHandle, &fileSizeStructure))
	if (!GetFileInformationByHandleEx(localFileHandle, FileStorageInfo, &fileSizeStructure, sizeof(FILE_STORAGE_INFO)))
    {
        TransferStatusText = "Cannot get the file size, " + GetLastErrorMsg();
        return false;
    }

    ULONGLONG fileSizeBytes = fileSizeStructure.QuadPart;
    float fileSizeMbytes = (float64) fileSizeBytes / 1024.00f / 1024.00f;
    ULONGLONG fileSizeBits = fileSizeBytes * 8;
    float fileSizeMbits = (float64) fileSizeBits / 1024.00f / 1024.00f;
    FileSize = fileSizeMbytes;
    StatusChanged(this, 0);

    HINTERNET ftpFile = FtpOpenFile(_inetFileInfo, 
                                    ftpFilePath->Data(), 
                                    GENERIC_WRITE, 
                                    _transferFlags, 
                                    NULL);
    if (ftpFile == NULL)
    {
        TransferStatusText = "Cannot open remote file, " + GetLastErrorMsg();
        InternetCloseHandle(_inetFileInfo);
        InternetCloseHandle(_inetSession);
        return false;
    }

    if (_writeBufferSize > 0)
    {
        if (!InternetSetOption(ftpFile, 
                               INTERNET_OPTION_WRITE_BUFFER_SIZE, 
                               &_writeBufferSize, 
                               sizeof(_writeBufferSize)))
        {
            TransferStatusText = "Setting write buffer size failed: " + GetLastErrorMsg();
            InternetCloseHandle(_inetFileInfo);
            InternetCloseHandle(_inetSession);
            return false;
        }
    }

    DWORD uploadedBytes = 0;   // Size of the uploaded data in one write
    DWORD sizeSum = 0;         // Size of all the uploaded data
    float sizeSumMbytes = 0;   // Size of all the uploaded data in MB
    float lapTimeSum = 0;      // Accumulative sum of single write call times
    const DWORD bufferSize = 102400 * 4;
    BYTE data[bufferSize];     // Data buffer to use in a local read call
    BYTE copy[bufferSize];     // Data copy buffer to use in a remote file write
    BYTE* dataBuffer = data;   // For saving the original buffer address
    BYTE* dataAddr = data;     // For adjusting the data buffer pointer if needed
    DWORD dataSize = sizeof(data); // The original size of the data buffer
    DWORD readBytes = 0;
    
    ULONGLONG startTime = GetTickCount64();

    do
    {
        // Read part of the local file
        if (!ReadFile(localFileHandle, data, bufferSize, &readBytes, NULL))
        {
            TransferStatusText = "Error reading local file: " + GetLastErrorMsg();
            StatusChanged(this, -1);
            status = false;
            break;
        }

        // Copy buffer for sending, reset original
        memcpy_s(copy, sizeof(copy), data, readBytes);
        dataAddr = data;
        dataSize = sizeof(data);
        FillMemory(dataAddr, dataSize, 0);

        ULONGLONG startLap = GetTickCount64();

        // Write the part to remote file
        if (!InternetWriteFile(ftpFile, copy, readBytes, &uploadedBytes))
        {
            TransferStatusText = "Error writing: " + GetLastErrorMsg();
            StatusChanged(this, -1);
            status = false;
            break;
        }
        else
        {
            // TIME
            ULONGLONG stopLap = GetTickCount64();
            ULONG lapTimeMs = stopLap - startLap;
            float lapTimeSec = (float) lapTimeMs / 1000.00f;
            lapTimeSum += lapTimeSec;

            // SIZE
            ULONGLONG uploadedBits = uploadedBytes * 8;
            float uploadedMbytes = (float64) uploadedBytes / 1024.00f / 1024.00f;
            float uploadedMbits = (float64) uploadedBits / 1024.00f / 1024.00f;
            sizeSum += uploadedBytes;
            sizeSumMbytes = (float64) sizeSum / 1024.00f / 1024.00f;
            int percentUploaded = (int) 100.00f * sizeSumMbytes / fileSizeMbytes;

            // BANDWIDTH
            float speedMbits = uploadedMbits / lapTimeSec; // Mbps
            float speedMbytes = uploadedMbytes / lapTimeSec; // MB/s
            if (speedMbits > MaxSpeed) 
            {
                MaxSpeed = speedMbits;
            }

            // UPDATE STATUS INFO
            Transferred = sizeSumMbytes;
            Time = lapTimeSum;
            SpeedBytes = speedMbytes;
            SpeedBits = speedMbits;
            StatusChanged(this, percentUploaded);
        }
    }
    while (uploadedBytes > 0);

    ULONGLONG endTime = GetTickCount64();

    InternetCloseHandle(ftpFile);
    InternetCloseHandle(_inetFileInfo);
    InternetCloseHandle(_inetSession);

    ULONG timeMs = endTime - startTime; // milliseconds
    float timeSec = (float) timeMs / 1000.00f;
    float avgSpeedMbits = fileSizeMbits / timeSec;
    float avgSpeedMbytes = fileSizeMbytes / timeSec;
    Transferred = sizeSumMbytes;
    Time = timeSec;
    SpeedBytes = avgSpeedMbytes;
    SpeedBits = avgSpeedMbits;
    StatusChanged(this, 100);

    return status;
}

bool FtpClient::DownloadFile(String^ localFilePath, String^ ftpFilePath)
{
    bool status = true;

    CREATEFILE2_EXTENDED_PARAMETERS  extParams;
    extParams.dwSize =               sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
    extParams.dwFileAttributes =     FILE_ATTRIBUTE_NORMAL;
    extParams.dwFileFlags =          FILE_FLAG_SEQUENTIAL_SCAN;
    extParams.dwSecurityQosFlags =   SECURITY_ANONYMOUS;
    extParams.lpSecurityAttributes = NULL;
    extParams.hTemplateFile =        NULL;
    
    HANDLE localFileHandle = CreateFile2(localFilePath->Data(), 
                                         GENERIC_READ | GENERIC_WRITE, 
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                         CREATE_ALWAYS, 
                                         &extParams);
    if (localFileHandle == INVALID_HANDLE_VALUE)
    {
        TransferStatusText = "Cannot create the file " + localFilePath + ", " + GetLastErrorMsg();
        return false;
    }
    
#ifdef ASYNC
    String^ eventName = "Local\\FtpDownloadFileWriteEvent";
    HANDLE fileWriteEvent = CreateEventEx(NULL, 
                                          eventName->Data(), 
                                          CREATE_EVENT_MANUAL_RESET, 
                                          SYNCHRONIZE | EVENT_MODIFY_STATE);
    if (fileWriteEvent == NULL)
    {
        TransferStatusText = "Error creating event: " + GetLastErrorMsg();
        return false;
    }
    SetEvent(fileWriteEvent);
#endif

    char16 firstCharInFilePath = ftpFilePath->Begin()[0];
    char16* slash = L"/";
    if (firstCharInFilePath == *slash)
    {
        Utils::_replace_in_place(ftpFilePath, slash, L"%2f");
    }
    
    HINTERNET inetFile = FtpOpenFile(_inetFileInfo, 
                                     ftpFilePath->Data(), 
                                     GENERIC_READ, 
                                     _transferFlags, 
                                     NULL);
    if (inetFile == NULL)
    {
        TransferStatusText = "Cannot open remote file for size, " + GetLastErrorMsg();
        InternetCloseHandle(_inetFileInfo);
        InternetCloseHandle(_inetSession);
        return false;
    }

    if (_readBufferSize > 0)
    {
        if (!InternetSetOption(inetFile, 
                               INTERNET_OPTION_READ_BUFFER_SIZE, 
                               &_readBufferSize, 
                               sizeof(_readBufferSize)))
        {
            TransferStatusText = "Setting read buffer size failed: " + GetLastErrorMsg();
            InternetCloseHandle(_inetFileInfo);
            InternetCloseHandle(_inetSession);
            return false;
        }
    }

    LPDWORD fileSizeHigh = 0;
    ULONGLONG fileSizeBytes = FtpGetFileSize(inetFile, fileSizeHigh);
    float fileSizeMbytes = (float64) fileSizeBytes / 1024.00f / 1024.00f;
    ULONGLONG fileSizeBits = fileSizeBytes * 8;
    float fileSizeMbits = (float64) fileSizeBits / 1024.00f / 1024.00f;
    FileSize = fileSizeMbytes;
    StatusChanged(this, 0);

    String^ inetFileUrl;
    if (!_userName->IsEmpty() || !_password->IsEmpty())
    {
        inetFileUrl = "ftp://" + _userName + ":" + _password + "@" + _host + "/" + ftpFilePath;
    }
    else
    {
        inetFileUrl = "ftp://" + _host + "/" + ftpFilePath;
    }

    _inetResource = InternetOpenUrl(_inetSession, inetFileUrl->Data(), NULL, 0, _transferFlags, 0);
    if (_inetResource == NULL)
    {
        TransferStatusText = "Cannot open remote file for read, " + GetLastErrorMsg();
        InternetCloseHandle(inetFile);
        InternetCloseHandle(_inetFileInfo);
        InternetCloseHandle(_inetSession);
        return false;
    }

    DWORD downloadedBytes = 0; // Size of the downloaded data in one read
    DWORD sizeSum = 0;         // Size of all the downloaded data
    float sizeSumMbytes = 0;   // Size of all the downloaded data in MB
    float lapTimeSum = 0;      // Accumulative sum of single read call times
    const DWORD bufferSize = 102400 * 4;
    BYTE data[bufferSize];     // Data buffer to use in a read call
    BYTE copy[bufferSize];     // Data copy buffer to use in a file write
    BYTE* dataBuffer = data;   // For saving the original buffer address
    BYTE* dataAddr = data;     // For adjusting the data buffer pointer if needed
    DWORD dataSize = sizeof(data); // The original size of the data buffer
    FillMemory(dataAddr, dataSize, 0); // Wipe the data buffer
    DWORD bytesWritten = 0;
    LPOVERLAPPED asyncInfo;

    ULONGLONG startTime = GetTickCount64();

    do
    {
        ULONGLONG startLap = GetTickCount64();

        if (!InternetReadFile(inetFile, dataAddr, dataSize, &downloadedBytes))
        {
            TransferStatusText = "Error reading: " + GetLastErrorMsg();
            StatusChanged(this, -1);
            status = false;
            break;
        }
        else
        {
            // TIME
            ULONGLONG stopLap = GetTickCount64();
            ULONG lapTimeMs = stopLap - startLap;
            float lapTimeSec = (float) lapTimeMs / 1000.00f;
            lapTimeSum += lapTimeSec;

            // SIZE
            ULONGLONG downloadedBits = downloadedBytes * 8;
            float downloadedMbytes = (float64) downloadedBytes / 1024.00f / 1024.00f;
            float downloadedMbits = (float64) downloadedBits / 1024.00f / 1024.00f;
            sizeSum += downloadedBytes;
            sizeSumMbytes = (float64) sizeSum / 1024.00f / 1024.00f;
            int percentDownloaded = (int) 100.00f * sizeSumMbytes / fileSizeMbytes;

            // BANDWIDTH
            float speedMbits = downloadedMbits / lapTimeSec; // Mbps
            float speedMbytes = downloadedMbytes / lapTimeSec; // MB/s
            if (speedMbits > MaxSpeed) 
            {
                MaxSpeed = speedMbits;
            }

            // UPDATE STATUS INFO
            Transferred = sizeSumMbytes;
            Time = lapTimeSum;
            SpeedBytes = speedMbytes;
            SpeedBits = speedMbits;
            StatusChanged(this, percentDownloaded);

#ifdef ASYNC
            IAsyncAction^ workItem;
            workItem->Completed = ref new AsyncActionCompletedHandler([fileWriteEvent](IAsyncAction^ asyncInfo, AsyncStatus asyncStatus)
            {
                if (asyncStatus == AsyncStatus::Canceled)
                {
                    return;
                }
                SetEvent(fileWriteEvent);
            });

            DWORD result = WaitForSingleObjectEx(fileWriteEvent, 1000, false);
            if (result == WAIT_FAILED || result == WAIT_TIMEOUT )
            {
                TransferStatusText = "Error waiting file write event: " + GetLastErrorMsg();
                StatusChanged(this, -1);
                status = false;
                break;
            }
#endif
            
            // COPY AND RESET BUFFER
            memcpy_s(copy, sizeof(copy), data, downloadedBytes);
            dataAddr = data;
            dataSize = sizeof(data);
            FillMemory(dataAddr, dataSize, 0);

            if (downloadedBytes > 0)
            {
#ifdef ASYNC
                workItem = ThreadPool::RunAsync(ref new WorkItemHandler([this, status, localFileHandle, &copy, downloadedBytes, &bytesWritten, fileWriteEvent](IAsyncAction^ operation)
                {
                    ResetEvent(fileWriteEvent);
                    if (!WriteFile(localFileHandle, copy, downloadedBytes, &bytesWritten, NULL))
                    {
                        operation->Cancel();
                        TransferStatusText = "Error writing file: " + GetLastErrorMsg();
                    }
                }));
#else
                if (!WriteFile(localFileHandle, copy, downloadedBytes, &bytesWritten, NULL))
                {
                    TransferStatusText = "Error writing file: " + GetLastErrorMsg();
                    status = false;
                    break;
                }
#endif
            }
        }
    }
    while (downloadedBytes > 0);

    ULONGLONG endTime = GetTickCount64();

#ifdef ASYNC
    CloseHandle(fileWriteEvent);
#endif
    CloseHandle(localFileHandle);
    InternetCloseHandle(inetFile);
    InternetCloseHandle(_inetFileInfo);
    InternetCloseHandle(_inetResource);
    InternetCloseHandle(_inetSession);

    ULONG timeMs = endTime - startTime; // milliseconds
    float timeSec = (float) timeMs / 1000.00f;
    float avgSpeedMbits = fileSizeMbits / timeSec;
    float avgSpeedMbytes = fileSizeMbytes / timeSec;
    Transferred = sizeSumMbytes;
    Time = timeSec;
    SpeedBytes = avgSpeedMbytes;
    SpeedBits = avgSpeedMbits;
    StatusChanged(this, 100);

    return status;
}

bool FtpClient::ChangeRemoteDir(String^ newDirectory)
{
    bool status = true;

    if (!FtpSetCurrentDirectory(_inetFileInfo, newDirectory->Data()))
    {
        CloseSession();
        status = false;
    }

    return status;
}

String^ FtpClient::GetCurrentDir()
{
    wchar_t directoryPath[MAX_PATH];
    DWORD directoryPathLength = MAX_PATH;
    if (!FtpGetCurrentDirectory(_inetFileInfo, (LPWSTR) directoryPath, &directoryPathLength))
    {
        CloseSession();
        return ref new String(L"");
    }

    return ref new String(directoryPath);
}

IVectorView<DirectoryItem^>^ FtpClient::ListRemoteDir(/*String^* directory*/)
{
    bool status = true;

    std::vector<DirectoryItem^> dirListing;
    WIN32_FIND_DATA fileInfo;
    HINTERNET remoteDirectory = FtpFindFirstFile(_inetFileInfo, NULL, &fileInfo, INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, NULL);
    if (remoteDirectory == NULL)
    {
        return ref new VectorView<DirectoryItem^>();
    }

    do
    {
        DirectoryItem^ newItem = ref new DirectoryItem();
        
        std::wstring f(fileInfo.cFileName);
        int offset = f.find_last_of(L"/\\");
        newItem->Name = ref new String(f.substr(offset + 1).data());
        
        if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            newItem->IsDir = true;
        }
        else
        {
            wchar_t size[10];
            float fileSizeBytes = fileInfo.nFileSizeLow;
            float fileSize;
            String^ unit;
            bool sizeInBytes = false;
            if (fileSizeBytes > 1073741824) // giga bytes
            {
                fileSize = fileSizeBytes / 1073741824.00;
                unit = ref new String(L" GB");
            }
            else if (fileSizeBytes > 1048576) // mega bytes
            {
                fileSize = fileSizeBytes / 1048576.00;
                unit = ref new String(L" MB");
            }
            else if (fileSizeBytes > 1024) // kilo bytes
            {
                fileSize = fileSizeBytes / 1024.00;
                unit = ref new String(L" KB");
            }
            else // bytes
            {
                sizeInBytes = true;
                unit = ref new String(L" bytes");
            }

            if (sizeInBytes)
            {
                _itow_s(fileInfo.nFileSizeLow, size, 10, 10);
            }
            else
            {
                swprintf_s(size, 10, L"%.2f", fileSize);
            }
            newItem->SizeText = ref new String(size) + unit;
        }
        
        dirListing.push_back(newItem);
    } while (InternetFindNextFile(remoteDirectory, &fileInfo));

    InternetCloseHandle(remoteDirectory);
    // Need CloseSession()?
    
    return ref new VectorView<DirectoryItem^>(std::move(dirListing));
}

bool FtpClient::DeleteRemoteFile(String^ ftpFilePath)
{
    bool status = true;

    if (!FtpDeleteFile(_inetFileInfo, ftpFilePath->Data()))
    {
        CloseSession();
        status = false;
    }

    return status;
}

bool FtpClient::DeleteRemoteDirectory(String^ ftpDirectoryPath)
{
    bool status = true;

    if (!FtpRemoveDirectory(_inetFileInfo, ftpDirectoryPath->Data()))
    {
        CloseSession();
        status = false;
    }

    return status;
}

bool FtpClient::CreateRemoteDirectory(String^ ftpDirectoryPath)
{
    bool status = true;

    if (!FtpCreateDirectory(_inetFileInfo, ftpDirectoryPath->Data()))
    {
        CloseSession();
        status = false;
    }

    return status;
}

String^ FtpClient::GetLastErrorMsg()
{
    return Utils::GetLastErrorMsg();
}


void FtpClient::SetCredentials(String^ userName, String^ password)
{
    /*bool found = false;
    do {
        found = Utils::_replace_in_place(userName, L"@", L"%40");
    } while(found);
    
    do {
        found = Utils::_replace_in_place(password, L"@", L"%40");
    } while(found);*/

    _userName = userName;
    _password = password;
}

void FtpClient::SetOptions(int64 readBufferSize, int64 writeBufferSize)
{
    _readBufferSize = readBufferSize;
    _writeBufferSize = writeBufferSize;
}

HttpClient::HttpClient() {}

HttpClient::HttpClient(String^ userAgent)
{
    _userAgent = userAgent->Data();
    _userName = NULL;
    _password = NULL;
}

HttpClient::~HttpClient()
{
    Close();
}

int HttpClient::Connect(String^ host, bool secureConnection)
{
    if (secureConnection)
    {
        _transferFlags = INTERNET_FLAG_RELOAD | 
                INTERNET_FLAG_KEEP_CONNECTION | 
                INTERNET_FLAG_NO_CACHE_WRITE |
                INTERNET_FLAG_NO_COOKIES | 
                INTERNET_FLAG_PRAGMA_NOCACHE |
                INTERNET_FLAG_SECURE |
                INTERNET_FLAG_IGNORE_CERT_CN_INVALID;
        _port = INTERNET_DEFAULT_HTTPS_PORT;
    }
    else
    {
        _transferFlags = INTERNET_FLAG_RELOAD | 
                INTERNET_FLAG_KEEP_CONNECTION | 
                INTERNET_FLAG_NO_CACHE_WRITE | 
                INTERNET_FLAG_NO_COOKIES | 
                INTERNET_FLAG_PRAGMA_NOCACHE;
        _port = INTERNET_DEFAULT_HTTP_PORT;
    }

    int error = NOERROR;

    _inetSession = InternetOpen(_userAgent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (_inetSession != NULL)
    {
        _mode = INTERNET_SERVICE_HTTP;
        _host = host;
        error = InitializeSession();
    }
    else
    {
        error = GetLastError();
    }

    return error;
}

int HttpClient::InitializeSession()
{
    int error = NOERROR;

    _inetResource = InternetConnect(_inetSession, _host->Data(), _port, _userName, _password, _mode, _transferFlags, 0);
    if (!_inetResource)
    {
        error = GetLastError();
    }

    return error;
}

void HttpClient::CloseSession()
{
    InternetCloseHandle(_inetResource);
    InternetCloseHandle(_inetSession);
}

bool HttpClient::Close()
{
    bool status = TRUE;

    if (_inetResource != NULL)
    {
        status = InternetCloseHandle(_inetResource);
    }

    if (_inetSession != NULL)
    {
        status = InternetCloseHandle(_inetSession);
    }

    return status;
}

bool HttpClient::AddHttpHeader(String^ headerItem)
{
    return HttpAddRequestHeaders(_inetResource, headerItem->Data(), headerItem->Length(), HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON);
}

bool HttpClient::DoHttpRequest(String^ resource, String^ requestMethod, const Array<uint8>^ requestData)
{
    bool status = true;
    String^ response;

    LPCTSTR acceptTypes[] = { TEXT("text/*"), TEXT("image/*"), NULL };
    HINTERNET httpHandle = HttpOpenRequest(_inetResource, requestMethod->Data(), resource->Data(), NULL, NULL, acceptTypes, _transferFlags, NULL);
    if (!httpHandle)
    {
        response = GetLastErrorMsg();
        status = false;
    }
    else
    {
        if (!HttpSendRequest(httpHandle, NULL, 0, requestData->Data, requestData->Length))
        {
            InternetCloseHandle(httpHandle);
        }
        else
        {
            LPTSTR data;        // Buffer for one read of data
            DWORD  dataSize;    // Size of the data available
            DWORD  downloaded;  // Size of the downloaded data in one read
            DWORD  sizeSum = 0; // Size of all the data
            LPTSTR mergeBuffer; // Buffer to merge all the downloaded data

            do
            {
                if (!InternetQueryDataAvailable(httpHandle, &dataSize, 0, 0))
                {
                    status = false;
                    break;
                }
                else
                {
                    data = new TCHAR[dataSize + 1];
                    if (!InternetReadFile(httpHandle, (LPVOID) data, dataSize, &downloaded))
                    {
                        status = false;
                        break;
                    }
                    else
                    {
                        data[downloaded] = '\0';
                        mergeBuffer = new TCHAR[sizeSum + downloaded + 1];
                        if (sizeSum != 0)
                        {
                            mergeBuffer[sizeSum] = '\0';
                        }
                        else
                        {
                            mergeBuffer[0] = '\0';
                        }

                        size_t destinationSize = _tcslen(mergeBuffer) + _tcslen(data) + 1;
                        HRESULT hr = StringCchCat(mergeBuffer, destinationSize, data);
                        if(SUCCEEDED(hr))
                        {
                            response = String::Concat(response, ref new String(mergeBuffer));
                            delete[] mergeBuffer;
                            delete[] data;
                            sizeSum = sizeSum + downloaded + 1;
                        }
                    }
                }
            }
            while (dataSize > 0);
            httpResponseData = response;
            InternetCloseHandle(httpHandle);
        }
    }

    return status;
}


String^ HttpClient::GetLastErrorMsg()
{
    return Utils::GetLastErrorMsg();
}

void HttpClient::SetCredentials(String^ userName, String^ password)
{
    _userName = userName->Data();
    _password = password->Data();
}

DirectoryItem::DirectoryItem() {}

DirectoryItem::~DirectoryItem() {}

String^ Utils::GetLastErrorMsg()
{
    const int errorMsgLength = 512;
    wchar_t errorMsg[errorMsgLength];
    String^ errorText = "";

    int error = GetLastError();
    if (error == ERROR_INTERNET_EXTENDED_ERROR)
    {
        DWORD lastError = 0;
        DWORD errorTextLength = errorMsgLength - 1;
        if (!InternetGetLastResponseInfo(&lastError, (LPWSTR) errorMsg, &errorTextLength))
        {
            error = GetLastError();
        }
        errorText = ref new String(errorMsg);
    }
    wchar_t code[10]; 
    _itow_s(error, code, 10, 10);
    String^ errorCode = ref new String(code);

    return errorCode + ": " + errorText;
}

char* Utils::_wtoc(wchar_t* w)
{
    size_t size = wcslen(w) + 1;
    size_t convertedChars = 0;
    char* c = new char[size];
    wcstombs_s(&convertedChars, c, size, w, _TRUNCATE);
    return c;
}

wchar_t* Utils::_ctow(char* c)
{
    size_t size = strlen(c) + 1;
    size_t convertedChars = 0;
    wchar_t* w = new wchar_t[size];
    mbstowcs_s(&convertedChars, w, size, c, _TRUNCATE);
    return w;
}

bool Utils::_replace_in_place(String^ source, wchar_t* toFind, wchar_t* replacement)
{
    std::wstring s(source->Data());
    std::wstring r(replacement);
    std::string::size_type found = s.find(toFind);
    if (found != std::string::npos)
    {
        s = s.replace(found, r.length(), r);
        source = ref new String(s.c_str());
        return true;
    }
    else
    {
        return false;
    }
}

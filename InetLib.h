#pragma once

using namespace Platform;
using namespace Windows::Foundation::Collections;

namespace InetLib
{
    ref class FtpClient;
    public delegate void InetRequestStatusChange(FtpClient^ sender, int percentDone);

    public ref class DirectoryItem sealed
    {
    public:
        DirectoryItem();
        virtual ~DirectoryItem();

        property bool IsDir;
        property String^ Name;
        property String^ SizeText;
        property String^ FullPath;
        property String^ IconPath;

    };

    public ref class FtpClient sealed
    {
    public:
        FtpClient();
        FtpClient(String^ userAgent);
        virtual ~FtpClient();

        event InetRequestStatusChange^ StatusChanged;

        int Connect(String^ host);
        void SetCredentials(String^ userName, String^ password);
        void SetOptions(int64 readBufferSize, int64 writeBufferSize);
        bool UploadFile(String^ localFilePath, String^ ftpFilePath);
        bool DownloadFile(String^ localFilePath, String^ ftpFilePath);
        bool ChangeRemoteDir(String^ newDirectory);
        bool DeleteRemoteFile(String^ ftpFilePath);
        bool CreateRemoteDirectory(String^ ftpDirectoryPath);
        bool DeleteRemoteDirectory(String^ ftpDirectoryPath);
        String^ GetCurrentDir();
        IVectorView<DirectoryItem^>^ ListRemoteDir();
        String^ GetLastErrorMsg();
        bool Close();

        property String^ TransferStatusText;
        property float Transferred;
        property float FileSize;
        property float Time;
        property float SpeedBytes;
        property float SpeedBits;
        property float MaxSpeed;
        
    private:
        int Connect(int mode, String^ host, int port, int flags);
        int InitializeSession();
        void CloseSession();

        const wchar_t* _userAgent;
        HINTERNET _inetSession;
        HINTERNET _inetFileInfo;
        HINTERNET _inetResource;
        String^ _userName;
        String^ _password;
        String^ _host;
        DWORD _transferFlags;
        int _port;
        int _mode;
        unsigned long _readBufferSize;
        unsigned long _writeBufferSize;
        
    };
    
    public ref class HttpClient sealed
    {
    public:
        HttpClient();
        HttpClient(String^ userAgent);
        virtual ~HttpClient();

        int Connect(String^ host, bool secureConnection);
        void SetCredentials(String^ userName, String^ password);
        bool AddHttpHeader(String^ headerItem);
        bool DoHttpRequest(String^ resource, String^ requestMethod, const Array<uint8>^ requestData);
        property String^ httpResponseData;
        String^ GetLastErrorMsg();
        bool Close();
                
    private:
        int Connect(int mode, String^ host, int port, int flags);
        int InitializeSession();
        void CloseSession();

        HINTERNET _inetSession;
        HINTERNET _inetResource;
        const wchar_t* _userAgent;
        const wchar_t* _userName;
        const wchar_t* _password;
        String^ _host;
        DWORD _transferFlags;
        int _port;
        int _mode;

    };

    class Utils
    {
    public:
        static char* _wtoc(wchar_t* w);
        static wchar_t* _ctow(char* c);
        static bool _replace_in_place(String^ source, wchar_t* toFind, wchar_t* replacement);
        static String^ GetLastErrorMsg();

    };
}

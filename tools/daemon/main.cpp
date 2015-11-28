/* ---------------------------------------------------------------------------
*file:      GCDaemon
*ingroup:   tools
*author:    oudream , luliangbin
*date:      2013/10/29
*brief:     window only
*           GCDaemon.exe -n 5561;5562 -t 6000 -p a.exe;b.exe
*           UDP Port  5561  TimeOut 6000 ms
*modify:    2013/10/29 create
---------------------------------------------------------------------------*/

#include "common.cpp"

#define GM_BUFFER_SZIE 4096

unsigned int f_iTimeOut_receive = 3000;

void fn_outInfo(const string& sInfo)
{
    string sMsg = sInfo;
    sMsg.append("\n\n");
    ::cout << sMsg;
}

typedef struct {
    char code[10];
    char date[4];
} SampleInfo;

class Processer
{
public:
    Processer(string sProcessFilePath, int iWaitMillisecond, int iProcessType, int iLocalPort, string sHeartBuffer, int iTimeOut_receive, string sProcessParam,int iMode)
    {
        exeFilePath = sProcessFilePath;
        exePath = fn_extractFilePath(sProcessFilePath);
        exeFileName = fn_extractFileName(sProcessFilePath);
        processType = iProcessType;
        waitFirstMillisecond = iWaitMillisecond;

        localPort = iLocalPort;
        heartBuffer = sHeartBuffer;
        heartHexString = fn_toHexstring(sHeartBuffer.data(), sHeartBuffer.size(), false);
        timeOut_receive = iTimeOut_receive;

        isThreadWaitFinish  = false;
        startTimes = 0;
        hasFirstReceive = false;
        isThreadReceiveError = false;
        isThreadReceiveCancel = false;
        lastReceiveTime = 0;
        notReceiveLong = 0;
        startMode = iMode;
        runParam = sProcessParam;

        commandLine = sProcessFilePath;
        if (sProcessParam.size() > 0)
        {
            commandLine.push_back(' ');
            commandLine.append(sProcessParam);
        }

        memset(&pi, 0, sizeof(pi));
    }

    ~Processer()
    {
        releaseProcess();
    }

    void resetReceiveData()
    {
        //��ʼ���̺߳������õ���ȫ�ֲ���
        hasFirstReceive = false;
        isThreadReceiveError = false;
        isThreadReceiveCancel = false;
        lastReceiveTime = 0;
        notReceiveLong = 0;
    }

    void outInfo(const string& sInfo)
    {
        fn_outInfo( exeFileName + " : " + sInfo );
    }

    static DWORD WINAPI fn_threadWaitProcess( LPVOID lpParam )
    {
        Processer * oProcesser = (Processer * ) lpParam;
        fn_outInfo( oProcesser->exeFileName + " : WaitProcess begin" );
        WaitForSingleObject( oProcesser->pi.hProcess, INFINITE );//�������Ƿ�ֹͣ
        fn_outInfo( oProcesser->exeFileName + fn_format( " : WaitProcess end : %d" , oProcesser->pi.dwProcessId ) );
        oProcesser->isThreadWaitFinish = true;
        return 0;
    }

    //����
    static DWORD WINAPI fn_threadReceive( LPVOID lpParam )
    {
        Processer * oProcesser = (Processer * ) lpParam;
        //��������
        SOCKADDR_IN clientAddr;
        int nClientLen = sizeof(clientAddr);
        char            buf[GM_BUFFER_SZIE];    //�������ݻ�����
        ZeroMemory(buf, GM_BUFFER_SZIE);

        int nTimeRpt = oProcesser->timeOut_receive * 3;
        msepoch_t dtLastReceive = fn_currentMsepoch();
        int iReceiveTotal = 0;
        int iReceiveSize = 0;
        string sReceive;

        while (!oProcesser->isThreadReceiveCancel)
        {
            iReceiveSize = ::recvfrom(oProcesser->socketId, buf, GM_BUFFER_SZIE, 0, (SOCKADDR*)&clientAddr, &nClientLen);
            if(iReceiveSize == SOCKET_ERROR)
            {
                fn_outInfo( oProcesser->exeFileName + " : " + fn_format("��������ʧ�ܣ�ʧ��ԭ��: %d\n", WSAGetLastError() ));
                oProcesser->isThreadReceiveError = true;
                return 1;
            }
            else if (iReceiveSize > 0)
            {
                sReceive.append(string(buf, iReceiveSize));
                if (sReceive.size() >= oProcesser->heartBuffer.size())
                {
                    size_t found = sReceive.rfind(oProcesser->heartBuffer);
                    if (found != string::npos)
                    {
                        oProcesser->lastReceiveTime = fn_currentMsepoch();
                        oProcesser->notReceiveLong = 0;
                        oProcesser->hasFirstReceive = true;
                        iReceiveTotal += iReceiveSize;
                        if((oProcesser->lastReceiveTime - dtLastReceive) > nTimeRpt)
                        {
                            string sLastReceiveTime = fn_toString(oProcesser->lastReceiveTime);
                            string sMsg = oProcesser->exeFileName + " : " + fn_format( "LastReceiveTime : %s ; TimeSpan : %lld ; ReceiveTotal : %d" , sLastReceiveTime.c_str() , (oProcesser->lastReceiveTime-dtLastReceive)/1000 , iReceiveTotal);
                            fn_outInfo( sMsg );
                            iReceiveTotal = 0;
                            dtLastReceive = oProcesser->lastReceiveTime;
                        }

                        sReceive = sReceive.substr(found + oProcesser->heartBuffer.size());
                    }
                    else
                    {
                        sReceive.resize(oProcesser->heartBuffer.size()-1);
                    }
                }
            }
        }
        return 0;
    }

    bool createSocket()
    {
        resetReceiveData();
        //�����׽���
        socketId = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socketId == INVALID_SOCKET)
        {
            outInfo( fn_format( "�����׽���ʧ�ܣ�ʧ��ԭ�� �� %d" , ::WSAGetLastError() ) );
            return false;
        }

        SOCKADDR_IN        servAddr;        //��������ַ
        //��������ַ
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = ::htons((short)localPort);            //�˿�
        servAddr.sin_addr.s_addr = htonl(INADDR_ANY);    //IP

        //��
        if (bind(socketId, (SOCKADDR *)&servAddr, sizeof(servAddr)) == SOCKET_ERROR)
        {
            outInfo( fn_format( "��ʧ�ܣ�ʧ��ԭ�� : %d " , ::WSAGetLastError() ) );
            releaseSocket();
            return false;
        }

        DWORD dwThreadId2;
        socketThread = ::CreateThread(
                    NULL,              // default security attributes
                    0,                 // use default stack size
                    fn_threadReceive,          // thread function
                    this,             // argument to thread function
                    0,                 // use default creation flags
                    &dwThreadId2);   // returns the thread identifier
        return socketThread != NULL;
    }

    void releaseSocket()
    {
        if (socketThread)
        {
            ::CloseHandle(socketThread);
            socketThread = NULL;
        }
        if (socketId)
        {
            ::closesocket(socketId);
            socketId = 0;
        }
    }

    void restartSocket()
    {
        releaseSocket();
        createSocket();
    }

    bool createProcess()
    {
        DWORD dwCreationFlags = 0;
//        dwCreationFlags |= CREATE_NEW_CONSOLE;
        if(startMode==0)dwCreationFlags |= CREATE_NO_WINDOW;
        else if(startMode==1)dwCreationFlags |= CREATE_NEW_CONSOLE;

        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
//        fn_killProcess(const_cast<char *>(exeFileName.data()));
        // �����ӽ��̣��ж��Ƿ�ִ�гɹ�
        if(! ::CreateProcessA( NULL, const_cast<char *>(commandLine.data()),NULL,NULL,FALSE,dwCreationFlags,NULL,exePath.c_str(),&si,&pi) )
        {
            outInfo( fn_format( "��������ʧ�� : %d" , ::GetLastError() ) );
//            system("pause"); //���ڲ���
            return 0;
        }

        startTimes++;
        //����ִ�гɹ�����ӡ������Ϣ
        string sMsg = fn_format("�������ӽ��̵���Ϣ :\n����ID pi.dwProcessID : %d\n�߳�ID pi.dwThreadID : %d\n�������� startTimes : %d" , pi.dwProcessId , pi.dwThreadId, startTimes);
        outInfo(sMsg);
        DWORD dwThreadId;
        isThreadWaitFinish = false;
        waitProcessThread = ::CreateThread(
                    NULL,              // default security attributes
                    0,                 // use default stack size
                    fn_threadWaitProcess,          // thread function
                    this,             // argument to thread function
                    0,                 // use default creation flags
                    &dwThreadId);   // returns the thread identifier
        bool bSuccess = waitProcessThread != NULL;
        if (bSuccess) startTime = fn_currentMsepoch();
        return bSuccess;
    }

    void releaseProcess()
    {
        if (0 == pi.hProcess)
            return;
        ::TerminateProcess(pi.hProcess, 0);
        ::WaitForSingleObject( pi.hProcess, INFINITE );//����߳��Ƿ�ֹͣ
        ::CloseHandle(waitProcessThread);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        waitProcessThread = 0;
        pi.hProcess = 0;
        pi.hThread = 0;
        string sMsg = fn_format("�ӽ����Ѿ��˳� : isThreadWaitFinish == %d, hasFirstReceive == %d, notReceiveLong == %d", (int)isThreadWaitFinish, (int)hasFirstReceive, notReceiveLong);
        outInfo( sMsg );
    }

    void rerunProcess()
    {
        releaseProcess();
        hasFirstReceive = false;
        notReceiveLong = 0;
        createProcess();
    }

public:
    string exeFilePath;
    string exePath;
    string exeFileName;
    int processType;
    int waitFirstMillisecond;

    int localPort;
    string heartHexString;
    string heartBuffer;
    unsigned int timeOut_receive;

    int startTimes;
    msepoch_t startTime;

    //����
    PROCESS_INFORMATION pi; //������Ϣ
    HANDLE waitProcessThread;
    volatile bool isThreadWaitFinish;

    //�׽���
    SOCKET socketId;
    HANDLE socketThread;

    //��������
    volatile bool hasFirstReceive;
    volatile bool isThreadReceiveError;
    volatile bool isThreadReceiveCancel;
    msepoch_t lastReceiveTime;
    volatile unsigned int notReceiveLong;

    string runParam;
    string commandLine;
    int    startMode; //����ģʽ
};

vector<Processer*> f_oProcesses;

//#pf [process file]��Ӧ�ó����ļ�������д���·�������·��
//#pp [process param]���̵Ĳ���
//#pt [process type]��Ӧ�ó�������32λ�����λ��ʾ�Ƿ���ĳ�������Ǻ��ģ��˳���һ��over�ͱ���ȫ��Ӧ�ó�����������
//#pw [process wait]��Ӧ�ó����״������ȴ��೤ʱ����������һ�� --{Ĭ��Ϊ�������̺󲻵ȴ�}
//#np [network port]����������˿ں��б����� --{Ĭ��Ϊ�գ������������ж�}
//#nt [network timeout]�����糬ʱ��û���յ����ݵĳ�ʱ --{Ĭ��Ϊ"5000"}
//#nh [network heart]�������������� --{Ĭ��Ϊ"a55aa55a"}
//#pm [process mode]���̵�ģʽ
void fn_prepareProcessInfo2()
{
    vector<string> sLines;
    fn_fileLoad(f_sApplicationConfigFilePath, sLines, "\r\n");
    vector<vector<string> > sInfoeses;
    sInfoeses = fn_split(sLines, '[', '#');
    vector<string> sCmdFulls;
    vector<int> iPortes;
    for (size_t i = 0; i < sInfoeses.size(); ++i)
    {
        const vector<string> & sInfoes = sInfoeses.at(i);
        map<string, string> sProcessInfos = fn_splitToMap(sInfoes, '=');
        //pf=aaa/aaa.exe,pt=1,pw=3000,np=5566,nt=6600,nh=a55aa55a

        string sExeFilePath;
        int iProcessType;
        int iWaitFirstMillisecond;
        int iLocalPort;
        string sHeartBuffer;
        int iTimeOut_receive;

        string sPf = sProcessInfos["pf"];
        if (sPf.empty())
        {
            cout << "config index : " << i << " �ǿ�·��" << endl;
            continue;
        }
        if (sPf.find(':') == string::npos)
        {
            sExeFilePath = fn_mergeFilePath(f_sApplicationPath, sPf);
        }
        else
        {
            sExeFilePath = sPf;
        }


        string sPp = sProcessInfos["pp"];
        if (sPp.size() > 255)
        {
            cout << "config index : " << i << " ���̵����в����������Ϸ�" << endl;
            continue;
        }


        string sCommandLine = sExeFilePath;
        if (sPp.size() > 0)
        {
            sCommandLine.push_back(' ');
            sCommandLine.append(sPp);
        }
        sCommandLine = fn_toLower(sCommandLine);
        if (find(sCmdFulls.begin(), sCmdFulls.end(), sCommandLine) != sCmdFulls.end())
        {
            cout << "config index : " << i << " ���ظ���������" << endl;
            continue;
        }
        sCmdFulls.push_back(sCommandLine);


        string sPt = sProcessInfos["pt"];
        iProcessType = atoi(sPt.c_str());


        string sPw = sProcessInfos["pw"];
        iWaitFirstMillisecond = atoi(sPw.c_str());
        if (! (iWaitFirstMillisecond >= 0 && iWaitFirstMillisecond < 60 * 1000 * 30))
        {
            cout << "config index : " << i << " �״������ȴ��೤ʱ�����ֵ���Ϸ�" << endl;
            continue;
        }


        string sNp = sProcessInfos["np"];
        iLocalPort = atoi(sNp.c_str());

        if (! (iLocalPort >= 0 && iLocalPort < USHRT_MAX))
        {
            cout << "config index : " << i << " �˿ڲ��Ϸ�" << endl;
            continue;
        }
        if (iLocalPort > 0 && find(iPortes.begin(), iPortes.end(), iLocalPort) != iPortes.end())
        {
            cout << "config index : " << i << " ���ظ��Ķ˿�" << endl;
            continue;
        }
        iPortes.push_back(iLocalPort);


        string sNt = sProcessInfos["nt"];
        iTimeOut_receive = atoi(sNt.c_str());
        if (! (iTimeOut_receive >= 0 && iTimeOut_receive < 86400*1000))
        {
            cout << "config index : " << i << " ���糬ʱ����ֵ���Ϸ�" << endl;
            continue;
        }


        string sNh = sProcessInfos["nh"];
        sHeartBuffer = sNh;
        if (sHeartBuffer.size() > 0)
        {
            if (! (fn_isValidHexCharater(sHeartBuffer) && (sHeartBuffer.size() % 2 == 0) && (sHeartBuffer.size() < 255) ) )
            {
                cout << "config index : " << i << " �������Ĳ��Ϸ�" << endl;
                continue;
            }
        }
        vector<char> hBuf = fn_fromHexstring(sHeartBuffer);
        string sHBuf = string(hBuf.data(), hBuf.size());


        string sMode = sProcessInfos["pm"];
        int iMode = atoi(sMode.data());
        if (! (iMode >= 0 && iMode < 5))
        {
            cout << "config index : " << i << " ����ģʽ������" << endl;
            continue;
        }


        Processer * oProcesser = new Processer(sExeFilePath, iWaitFirstMillisecond, iProcessType, iLocalPort, sHBuf, iTimeOut_receive,sPp,iMode);
        f_oProcesses.push_back(oProcesser);
        fn_outInfo( fn_format( "Append Process FilePath : %s \nLocal Port : %d\nHeart String : %d\n", sExeFilePath.c_str(), iLocalPort, sHeartBuffer.c_str() ) );
    }
}

void fn_handler (int param)
{
  std::cout << "recv signal : interactive attention : " << param << std::endl;
  std::cout << "but the application is must execute , can not exit!!!" << param << std::endl;

  for (size_t i = 0; i < f_oProcesses.size(); ++i)
  {
      Processer * oProcesser = f_oProcesses.at(i);
      oProcesser->releaseProcess();
  }
}

//main
int main(int argc, char *argv[])
{
    cout << "begin daemon:" << endl;

    fn_void_int_t prev_handler = signal (SIGINT, fn_handler);
    if (prev_handler == SIG_ERR)
    {
        std::cout << "error : signal (SIGINT , *)" << std::endl;
    }

    fn_application_init(argc, argv);

    fn_prepareProcessInfo2();

    if (f_oProcesses.empty())
    {
        fn_outInfo( "û��Ҫ�ػ���Ӧ�ó��򣬼�����������ȷ��!!!" );
        ::system("pause");
        return 0;
    }

    fn_outInfo(string("daemon start time : ") + fn_toString(fn_currentMsepoch()));

    WSADATA            wsd;            //WSADATA����
    //��ʼ���׽��ֶ�̬��
    if (::WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        fn_outInfo("��ʼ���׽��ֶ�̬��ʧ��!!!");
        ::system("pause");
        return 0;
    }

    for (size_t i = 0; i < f_oProcesses.size(); ++i)
    {
        Processer * oProcesser = f_oProcesses.at(i);
        int ret = fn_killProcess(const_cast<char *>(oProcesser->exeFileName.data()));

        fn_outInfo( string("kill [") +fn_toString(ret)+"] ���� :" + oProcesser->exeFileName );

    }

    for (size_t i = 0; i < f_oProcesses.size(); ++i)
    {
        Processer * oProcesser = f_oProcesses.at(i);
        oProcesser->createProcess();
        if (oProcesser->localPort > 0 && oProcesser->localPort < USHRT_MAX)
        {
            oProcesser->createSocket();
        }
        if (oProcesser->waitFirstMillisecond > 0)
        {
            ::Sleep(oProcesser->waitFirstMillisecond);
            fn_outInfo( string("���� : ") + oProcesser->exeFileName + "�״������ȴ��೤ʱ����������һ��������" );
        }
    }

    msepoch_t dtPreNow = fn_currentMsepoch();
    unsigned int iMsgLong = 0;
    while (1)
    {
        msepoch_t dtNow = fn_currentMsepoch();
        unsigned int dwSpan = dtNow - dtPreNow;
        string sMsg;
        for (size_t i = 0; i < f_oProcesses.size(); ++i)
        {
            Processer * oProcesser = f_oProcesses.at(i);
            //1)exe�Ѿ��˳�; 2)�����ݳ�ʱ
//            if ( oProcesser->isThreadWaitFinish || ( oProcesser->hasFirstReceive && oProcesser->notReceiveLong > oProcesser->timeOut_receive ) )
            if ( oProcesser->isThreadWaitFinish || ( oProcesser->heartBuffer.size()>0 && oProcesser->notReceiveLong > oProcesser->timeOut_receive ) )
            {
                oProcesser->rerunProcess();
                continue;
            }
            else
            {
                unsigned int iLong = oProcesser->notReceiveLong + dwSpan;
                oProcesser->notReceiveLong = iLong;
            }

            //socket����������������
            if ( oProcesser->isThreadReceiveError )
            {
                oProcesser->restartSocket();
                continue;
            }

            if (iMsgLong > 1000 * 60)
            {
                sMsg += fn_format("��%d�� ��������[ %s ] - �����˿�[ %d ] - ��������[ %s ] \n", i+1, oProcesser->exeFileName.c_str(), oProcesser->localPort, oProcesser->heartHexString.c_str());
                sMsg += fn_format("1) ����ʱ�� : %d ���ӣ� ���� : %d �Σ� �Ƿ��յ����� : %d \n", (int)((dtNow - oProcesser->startTime) / (1000 * 60)), oProcesser->startTimes , (int)oProcesser->hasFirstReceive);
                sMsg += fn_format("2) �ڲ���Ϣ : isThreadWaitFinish == %d, hasFirstReceive == %d, notReceiveLong == %d \n\n", (int)oProcesser->isThreadWaitFinish, (int)oProcesser->hasFirstReceive, oProcesser->notReceiveLong);
            }
        }
        if (iMsgLong > 1000 * 60)
        {
            sMsg = fn_format("�ػ�%d��Ӧ�ó��������Ǹ�Ӧ�ó������Ϣ��\n\n", f_oProcesses.size()) + sMsg;
            fn_outInfo(sMsg);
            iMsgLong = 0;
        }
        iMsgLong += dwSpan;
        dtPreNow = dtNow;
        ::Sleep(100);
    }

    ::WSACleanup();        //�ͷ��׽�����Դ

    ::exit(0);

    return 0;
}


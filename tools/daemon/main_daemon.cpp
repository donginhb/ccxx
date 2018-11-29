#define GM_OS_WIN

#include "daemon_global.h"
#include <ccxx/cxthread.h>
#include <psapi.h>

#define GM_BUFFER_SZIE 4096


using namespace std;


CxMutex * fn_oLogInfoPushLock()
{
    static CxMutex m;
    return & m;
}
static CxMutex * f_oLogInfoPushLock = fn_oLogInfoPushLock();
static vector<string> f_sLogInfos;
static volatile int f_sLogInfoSize = 0;

void fn_pushInfo(const string &sInfo)
{
    CxMutexScope lock(f_oLogInfoPushLock);
    msepoch_t lMsepoch = CxTime::currentSystemTime();
    int y, m, d, h, mi, se, ms;
    CxTime::decodeLocalTm(lMsepoch, y, m, d, h, mi, se, ms);
    string sLogInfo = CxTime::toString(y, m, d, h, mi, se, ms) + " : " + sInfo;
    f_sLogInfos.push_back(sLogInfo);
    f_sLogInfoSize++;
}

void fn_write_log(const string &sPath, const string &sInfo, bool bAddDatetime=true)
{
    if (! CxFileSystem::isExist(sPath))
    {
        CxFileSystem::createDirMultiLevel(sPath, GM_FILE_MODE_IRWXU);
    }

    msepoch_t lMsepoch = CxTime::currentSystemTime();
    int y, m, d, h, mi, se, ms;
    CxTime::decodeLocalTm(lMsepoch, y, m, d, h, mi, se, ms);
    string sLogInfo = sInfo;
    if (bAddDatetime)
    {
        sLogInfo = CxTime::toString(y, m, d, h, mi, se, ms) + " : " + sInfo;
    }
    string sFullFileNmae = CxFileSystem::mergeFilePath(sPath, CxString::format("%04d-%02d-%02d.log", y, m, d));

    FILE *oFile = fopen(sFullFileNmae.c_str(), "ab+");
    if (oFile)
    {
        fwrite(const_cast<char *>(sLogInfo.data()), 1, sLogInfo.size(), oFile);
        fflush(oFile);
        fclose(oFile);
    }
}

void fn_outInfo(const string &sInfo)
{
    vector<string> sInfos;
    if (f_sLogInfoSize>0)
    {
        CxMutexScope lock(f_oLogInfoPushLock);
        for (int i = 0; i < f_sLogInfos.size(); ++i)
        {
            sInfos.push_back(CxString::newString(f_sLogInfos[i]));
        }
        f_sLogInfos.clear();
        f_sLogInfoSize = 0;
    }
    for (int i = 0; i < sInfos.size(); ++i)
    {
        string sMsg = sInfos[i];
        sMsg.append("\n\n");
        ::cout << sMsg;
        //д��־�ļ�
        fn_write_log(CxAppEnv::logPath(), sMsg, false);
    }
    string sMsg = sInfo;
    sMsg.append("\n\n");
    ::cout << sMsg;
    //д��־�ļ�
    fn_write_log(CxAppEnv::logPath(), sMsg);
}

typedef struct SampleInfo {
    char code[10];
    char date[4];
} SampleInfo;

class Processer {
public:
    Processer(const ProcessConfig &processConfig2)
    {
        mProcessConfig = processConfig2;

        mIsThreadWaitFinish = false;
        mIsThreadWaitFinishOutInfoTime = 0;
        mStartTimes = 0;
        mStartTime = 0;
        mIsThreadReceiveError = false;
        mIsThreadReceiveCancel = false;
        mLastReceiveTime = 0;
        mReceiveBytes = 0;

        memset(&mPid, 0, sizeof(mPid));
        clearInfo();
    }

    ~Processer()
    {
        releaseProcess();
    }

    void resetReceiveData()
    {
        //��ʼ���̺߳������õ���ȫ�ֲ���
        mIsThreadReceiveError = false;
        mIsThreadReceiveCancel = false;
        mLastReceiveTime = 0;
        mReceiveBytes = 0;
    }

    void outInfo(const string &sInfo)
    {
        fn_outInfo(mProcessConfig.exeFileName + " : " + sInfo);
    }

    static DWORD WINAPI fn_threadWaitProcess(LPVOID lpParam)
    {
        Processer *oProcesser = (Processer *) lpParam;
        fn_pushInfo(oProcesser->mProcessConfig.exeFileName + " : WaitProcess begin");
        WaitForSingleObject(oProcesser->mPid.hProcess, INFINITE);//�������Ƿ�ֹͣ
        fn_pushInfo(oProcesser->mProcessConfig.exeFileName
                   + CxString::format(" : WaitProcess end : %d", oProcesser->mPid.dwProcessId));
        oProcesser->mIsThreadWaitFinish = true;
        oProcesser->mIsThreadWaitFinishOutInfoTime = CxTime::currentSystemTime();
        //*
        ProcessAlarm processAlarm;
        processAlarm.aid = oProcesser->mProcessConfig.aid;
        processAlarm.ord = oProcesser->mProcessConfig.ord;
        processAlarm.process = oProcesser->mProcessConfig.exeFileName;
        processAlarm.alarmTime = CxTime::currentSystemTime();
        processAlarm.alarmType = ProcessAlarm::AlarmType_Finish;
        processAlarm.alarmMsg = "in WaitForSingleObject INFINITE.";
        DaemonGlobal::log(ProcessAlarm::getLogClass(), processAlarm.toJson());
        //*
        oProcesser->clearInfo();// �����˳�ʱҪ���������Ϣ
        return 0;
    }

    //����
    static DWORD WINAPI fn_threadReceive(LPVOID lpParam)
    {
        Processer *oProcesser = (Processer *) lpParam;
        //��������
        SOCKADDR_IN clientAddr;
        int nClientLen = sizeof(clientAddr);
        char buf[GM_BUFFER_SZIE];    //�������ݻ�����
        ZeroMemory(buf, GM_BUFFER_SZIE);

        int iReceiveSize = 0;
        string sReceive;

        while (!oProcesser->mIsThreadReceiveCancel)
        {
            iReceiveSize = ::recvfrom(oProcesser->mSocketId, buf, GM_BUFFER_SZIE, 0, (SOCKADDR *) &clientAddr, &nClientLen);
            if (iReceiveSize == SOCKET_ERROR)
            {
                fn_pushInfo(oProcesser->mProcessConfig.exeFileName + " : "
                           + CxString::format("��������ʧ�ܣ�ʧ��ԭ��: %d\n", WSAGetLastError()));
                oProcesser->mIsThreadReceiveError = true;
                return 1;
            }
            else if (iReceiveSize > 0)
            {
                sReceive.append(string(buf, iReceiveSize));
                if (sReceive.size() >= oProcesser->mProcessConfig.heartBuffer.size())
                {

                    size_t found = sReceive.rfind(oProcesser->mProcessConfig.heartBuffer);
                    //�������
                    if (found != string::npos)
                    {
                        oProcesser->mLastReceiveTime = CxTime::currentSystemTime();
                        oProcesser->mReceiveBytes += iReceiveSize;;
                        //��ȡʣ���ַ���
                        sReceive = sReceive.substr(found + oProcesser->mProcessConfig.heartBuffer.size());
                    }
                    else
                    {
                        if (sReceive.size() > oProcesser->mProcessConfig.heartBuffer.size() * 2)
                        {
                            sReceive.clear();
                        }
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
        mSocketId = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (mSocketId == INVALID_SOCKET)
        {
            outInfo(CxString::format("�����׽���ʧ�ܣ�ʧ��ԭ�� �� %d", ::WSAGetLastError()));
            return false;
        }

        SOCKADDR_IN servAddr;        //��������ַ
        //��������ַ
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = ::htons((short) mProcessConfig.localPort);            //�˿�
        servAddr.sin_addr.s_addr = htonl(INADDR_ANY);    //IP

        //��
        if (bind(mSocketId, (SOCKADDR *) &servAddr, sizeof(servAddr)) == SOCKET_ERROR)
        {
            outInfo(CxString::format("��ʧ�ܣ�ʧ��ԭ�� : %d ", ::WSAGetLastError()));
            releaseSocket();
            return false;
        }

        DWORD dwThreadId2;
        mSocketThread = ::CreateThread(
            NULL,              // default security attributes
            0,                 // use default stack size
            fn_threadReceive,          // thread function
            this,             // argument to thread function
            0,                 // use default creation flags
            &dwThreadId2);   // returns the thread identifier
        return mSocketThread != NULL;
    }

    void releaseSocket()
    {
        if (mSocketThread)
        {
            ::CloseHandle(mSocketThread);
            mSocketThread = NULL;
        }
        if (mSocketId)
        {
            ::closesocket(mSocketId);
            mSocketId = 0;
        }
    }

    void restartSocket()
    {
        releaseSocket();
        createSocket();
    }

    void createProcess()
    {
        //�������ݻ�����
        DWORD dwCreationFlags = 0;
        //        dwCreationFlags |= CREATE_NEW_CONSOLE;
        if (mProcessConfig.startMode == 0)dwCreationFlags |= CREATE_NO_WINDOW;
        else if (mProcessConfig.startMode == 1)dwCreationFlags |= CREATE_NEW_CONSOLE;

        mStartTime = CxTime::currentSystemTime();

        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&mPid, sizeof(mPid));
        //        CxProcess::killProcess(exeFileName);
        // �����ӽ��̣��ж��Ƿ�ִ�гɹ�
        if (!::CreateProcessA(NULL, const_cast<char *>(mProcessConfig.commandLine.data()), NULL, NULL, FALSE, dwCreationFlags, NULL, mProcessConfig.workPath.c_str(), &si, &mPid))
        {
            outInfo(CxString::format("��������ʧ�� : %d", ::GetLastError()));
            //system("pause"); //���ڲ���
            return;
        }

        mStartTimes++;
        //����ִ�гɹ�����ӡ������Ϣ
        string sMsg = CxString::format("�������ӽ��̵���Ϣ :\n����ID pi.dwProcessID : %d\n�߳�ID pi.dwThreadID : %d\n�������� startTimes : %d", mPid.dwProcessId, mPid.dwThreadId, mStartTimes);
        outInfo(sMsg);
        DWORD dwThreadId;
        mIsThreadWaitFinish = false;
        mIsThreadWaitFinishOutInfoTime = 0;
        mWaitProcessThread = ::CreateThread(
            NULL,              // default security attributes
            0,                 // use default stack size
            fn_threadWaitProcess,          // thread function
            this,             // argument to thread function
            0,                 // use default creation flags
            &dwThreadId);   // returns the thread identifier
//        bool bSuccess = waitProcessThread != NULL;
//        if (bSuccess) startTime = CxTime::currentSystemTime();
//        return bSuccess;
    }

    void releaseProcess()
    {
        if (0 == mPid.hProcess)
            return;
        ::TerminateProcess(mPid.hProcess, 0);
        ::WaitForSingleObject(mPid.hProcess, INFINITE);//����߳��Ƿ�ֹͣ
        ::CloseHandle(mWaitProcessThread);
        ::CloseHandle(mPid.hProcess);
        ::CloseHandle(mPid.hThread);
        mWaitProcessThread = 0;
        mPid.hProcess = 0;
        mPid.hThread = 0;
        clearInfo();// �����˳�ʱҪ���������Ϣ
        string sMsg = CxString::format("�ӽ����Ѿ��˳� : isThreadWaitFinish == %d, now - lastReceiveTime == %lld", (int) mIsThreadWaitFinish,
                                       (CxTime::currentSystemTime() - mLastReceiveTime) / 1000);
        outInfo(sMsg);
        mLastReceiveTime = 0;
    }

    void rerunProcess()
    {
        releaseProcess();
        createProcess();
    }

    static void updateSysInfo()
    {
        //��һ�ε�ʱ��
        static long long last_idleTime_ = 0;
        static long long last_system_time_ = 0;
        LARGE_INTEGER idleTime;
        LARGE_INTEGER kernelTime;
        LARGE_INTEGER userTime;
        long long totalTime;

        ::GetSystemTimes((LPFILETIME) &idleTime, (LPFILETIME) &kernelTime, (LPFILETIME) &userTime);
        totalTime = kernelTime.QuadPart + userTime.QuadPart;
        if (last_system_time_ != 0 && totalTime > last_system_time_)
        {
            s_iSystemTimeDelta = totalTime - last_system_time_;
            s_SysCpuRatio = ((((s_iSystemTimeDelta - (idleTime.QuadPart - last_idleTime_)) /
                        ((s_iSystemTimeDelta / 1000) ? (s_iSystemTimeDelta / 1000) : 1)
                       ) + 5) / 10);
            if (s_SysCpuRatio >= 100)
            {
                s_SysCpuRatio = 99;
            }
        }
        last_system_time_ = totalTime;
        last_idleTime_ = idleTime.QuadPart;
        //��ȡsystem�ڴ�ʹ����
        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof(statex);
        if (::GlobalMemoryStatusEx(&statex))
        {
            s_SysMemeryRatio = statex.dwMemoryLoad; // �ڴ�ʹ����
        }
    }

    void updateCpuUsage() // ��ȡCPUʹ����
    {
        if (mPid.hProcess == 0)
            return;
        LARGE_INTEGER creation_time;
        LARGE_INTEGER exit_time;
        LARGE_INTEGER kernel_time;
        LARGE_INTEGER user_time;
        long long proctime;
        if (!GetProcessTimes(mPid.hProcess, (LPFILETIME) &creation_time, (LPFILETIME) &exit_time, (LPFILETIME) &kernel_time, (LPFILETIME) &user_time))
        {
            return;
        }
        proctime = kernel_time.QuadPart + user_time.QuadPart;
        if (mCpuTime == 0 || s_iSystemTimeDelta == 0)
        {
            mCpuRatio = 0;
        }
        else
        {
            if (mCpuTime > proctime)
            {
                mCpuTime = 0;
            }
            mCpuRatio = ((((proctime - mCpuTime) /
                     ((s_iSystemTimeDelta / 1000) ? (s_iSystemTimeDelta / 1000) : 1)
                    ) + 5) / 10);
            if (mCpuRatio >= 100)
            {
                mCpuRatio = 99;
            }
        }
        mCpuTime = proctime;
        return;
    }

    void updateMemoryUsage() // ��ȡ�ڴ�ʹ����
    {
        if (mPid.hProcess == 0)
            return;
        PROCESS_MEMORY_COUNTERS pmc;
        if (::GetProcessMemoryInfo(mPid.hProcess, &pmc, sizeof(pmc)))
        {
            mMemerySize = pmc.WorkingSetSize;

        }
    }

    void clearInfo()
    {
        mMemerySize = 0;
        mCpuRatio = 0;
        mCpuTime = 0;
    }

    void updateProcessInfo()
    {
        if (!mIsThreadWaitFinish)
        {
            updateCpuUsage();
            updateMemoryUsage();
        }
    }

    void updateAndLog()
    {
        updateProcessInfo();

        ProcessStatus processStatus;
        processStatus.aid = mProcessConfig.aid;
        processStatus.ord = mProcessConfig.ord;
        processStatus.process = mProcessConfig.exeFileName;
        processStatus.isRunnig = ! mIsThreadWaitFinish && mPid.hProcess != 0 ? TRUE : FALSE;
        processStatus.startTime = mStartTime;
        processStatus.startTimes = mStartTimes;
        processStatus.receiveBytes = mReceiveBytes;
        processStatus.lastReceiveTime = mLastReceiveTime;
        processStatus.recordTime = CxTime::currentSystemTime();
        processStatus.syscpu = s_SysCpuRatio;
        processStatus.sysmem = s_SysMemeryRatio;
        processStatus.cpu = mCpuRatio;
        processStatus.mem = mMemerySize;
        DaemonGlobal::log(ProcessStatus::getLogClass(), processStatus.toJson());

        cout << "pid:" << mPid.dwProcessId << "\tcpu:" << (int) mCpuRatio << "%\tmem:"
             << mMemerySize / 1024 << "k\t\t:" << mProcessConfig.exeFileName
             << mIsThreadWaitFinish
             << endl;
    }

public:
    ProcessConfig mProcessConfig;

    int mStartTimes;// ��������
    msepoch_t mStartTime; // ����ʱ��

    //����
    PROCESS_INFORMATION mPid; //������Ϣ
    HANDLE mWaitProcessThread;
    volatile bool mIsThreadWaitFinish;
    msepoch_t mIsThreadWaitFinishOutInfoTime;

    //�׽���
    SOCKET mSocketId;
    HANDLE mSocketThread;

    //��������
    volatile bool mIsThreadReceiveError;
    volatile bool mIsThreadReceiveCancel;
    msepoch_t mLastReceiveTime;
    long long mReceiveBytes;

    //��Դͳ��
    static unsigned long long s_iSystemTimeDelta; // ϵͳʱ������
    static unsigned char s_SysCpuRatio; // cpuռ���� 0 - 100
    static unsigned char s_SysMemeryRatio; // �ڴ�ռ���� 0 - 100
    unsigned int mCpuRatio; // cpuռ���� 0 - 100
    unsigned int mMemerySize; // ռ���ڴ� �ֽ�
    long long mCpuTime; // CPUʱ��

    int mKillTimes;

};
unsigned long long Processer::s_iSystemTimeDelta = 0; // ϵͳʱ������
unsigned char Processer::s_SysCpuRatio = 0; // cpuռ���� 0 - 100
unsigned char Processer::s_SysMemeryRatio = 0; // �ڴ�ռ���� 0 - 100

vector<Processer *> f_oDaemonProcesses;
vector<string> f_oKillProcesses;
vector<Processer *> f_oPerformanceProcesses;

std::vector<CxProcess::ProcessInfo> _onlineProcessInfos;

Processer *fn_findPerformanceProcess(const string &sProcessName)
{
    string sPrefix = CxString::toLower(CxFileSystem::extractFilePrefixName(sProcessName));
    for (size_t i = 0; i < f_oPerformanceProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oPerformanceProcesses.at(i);
        string sPrefix2 = CxString::toLower(CxFileSystem::extractFilePrefixName(oProcesser->mProcessConfig.exeFilePath));
        if (sPrefix == sPrefix2)
        {
            return oProcesser;
        }
    }
    return NULL;
}

/*
#pf [process file]��Ӧ�ó����ļ�������д���·�������·��
#pp [process param]���̵Ĳ���
#pt [process type]��Ӧ�ó�������32λ�����λ��ʾ�Ƿ���ĳ�������Ǻ��ģ��˳���һ��over�ͱ���ȫ��Ӧ�ó�����������
#pw [process wait]��Ӧ�ó����״������ȴ��೤ʱ����������һ�� --{Ĭ��Ϊ�������̺󲻵ȴ�}
#np [network port]����������˿ں��б����� --{Ĭ��Ϊ�գ������������ж�}
#nt [network timeout]�����糬ʱ��û���յ����ݵĳ�ʱ --{Ĭ��Ϊ"5000"}
#nh [network heart]�������������� --{Ĭ��Ϊ"a55aa55a"}
#pm [process mode]���̵�ģʽ
#dm [damon  mode]�ػ���ʽ  0 Ĭ�������ػ�,1 ����������
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'AppID', 'aid');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'OrderNo', 'ord');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'AppName', 'pf');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'AppPath', 'ppf');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'AppParam', 'pp');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'CanNotRunning', 'dm');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'WorkPath', 'pwd');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'AllOver', 'pt');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'OperationalCfg', 'OperationalCfg');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'OperationalTm', 'OperationalTm');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'StaySleep', 'pw');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'HeartbeatPort', 'np');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'HeartbeatTimeout', 'nt');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'HeartbeatData', 'nh');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'WithWin', 'pm');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'CpuLimit', 'CpuLimit');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'MemLimit', 'MemLimit');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'AppAlias', 'AppAlias');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'Supervise', 'Supervise');
INSERT INTO ini_tableinfo (TableName, ColumnName, EntryName) VALUES('ha_appconfig', 'RunMode', 'RunMode');
AppID=aid,OrderNo=ord,AppName=pf,AppPath=ppf,AppParam=pp,CanNotRunning=dm,WorkPath=pwd,AllOver=pt,OperationalCfg=OperationalCfg,OperationalTm=OperationalTm,StaySleep=pw,HeartbeatPort=np,HeartbeatTimeout=nt,HeartbeatData=nh,WithWin=pm
,CpuLimit=CpuLimit,MemLimit=MemLimit,AppAlias=AppAlias,Supervise=Supervise,RunMode=RunMode
*/
void fn_prepareProcessers(const vector<map<string, string> > &processConfigData)
{
    f_oKillProcesses.clear();

    std::vector<ProcessConfig> processConfigs = ProcessConfig::prepareProcessers(processConfigData);
    for (size_t i = 0; i < processConfigs.size(); ++i)
    {
        ProcessConfig processConfig = processConfigs.at(i);
        if (processConfig.dm == ProcessConfig::DmCannotRunning)
        {
            f_oKillProcesses.push_back(processConfig.exeFileName);
            fn_outInfo(CxString::format("Append Kill Process. exeFilePath: %s", processConfig.exeFilePath.c_str()));
            fn_write_log(CxAppEnv::logPath(), CxString::join(ProcessConfig::report(processConfig), "\r\n"));
            continue;
        }
        if (processConfig.RunMode == ProcessConfig::RunModeAuto)
        {
            Processer *oProcesser = new Processer(processConfig);
            f_oDaemonProcesses.push_back(oProcesser);
            fn_outInfo(CxString::format("Append Daemon Process. exeFilePath: %s", processConfig.exeFilePath.c_str()));
            fn_write_log(CxAppEnv::logPath(), CxString::join(ProcessConfig::report(processConfig), "\r\n"));
            continue;
        }
        if (processConfig.Supervise == ProcessConfig::SupervisePerformance || processConfig.Supervise == ProcessConfig::SuperviseAB)
        {
            Processer *oProcesser = new Processer(processConfig);
            f_oPerformanceProcesses.push_back(oProcesser);
            fn_outInfo(CxString::format("Append Performance Process. exeFilePath: %s", processConfig.exeFilePath.c_str()));
            fn_write_log(CxAppEnv::logPath(), CxString::join(ProcessConfig::report(processConfig), "\r\n"));
            continue;
        }
        fn_outInfo(CxString::format("Warning : Do not Append Process! exeFilePath: %s", processConfig.exeFilePath.c_str()));
        fn_write_log(CxAppEnv::logPath(), CxString::join(ProcessConfig::report(processConfig), "\r\n"));
    }
    fn_outInfo("load Process Config Sum Total : " + CxString::toString(processConfigs.size()));
    fn_outInfo("load Process KillProcesses Count : " + CxString::toString(f_oKillProcesses.size()));
    fn_outInfo("load Process DaemonProcesses Count : " + CxString::toString(f_oDaemonProcesses.size()));
    fn_outInfo("load Process PerformanceProcesses Count : " + CxString::toString(f_oPerformanceProcesses.size()));
}

void fn_loadProcessers()
{
    vector<map<string, string> > processConfigs;
    if (CxString::endWithCase(CxAppEnv::configFilePath(), ".ini"))
    {
        vector<string> sLines;
        CxFile::load(CxAppEnv::configFilePath(), sLines, "\r\n");
        vector<vector<string> > sInfoeses;
        sInfoeses = CxSkverIni::split(sLines, '[', '#');

        for (size_t i = 0; i < sInfoeses.size(); ++i)
        {
            const vector<string> &sInfoes = sInfoeses.at(i);
            processConfigs.push_back(CxString::splitToMap(sInfoes, '='));
        }
        fn_prepareProcessers(processConfigs);
    }
    else
    {
        fn_outInfo("Error ! Error ! Error ! : config file must be ini-file.");
    }
}

void fn_handler(int param)
{
    std::cout << "recv signal : interactive attention : " << param << std::endl;
    std::cout << "but the application is must execute , can not exit!!!" << param << std::endl;

    for (size_t i = 0; i < f_oDaemonProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oDaemonProcesses.at(i);
        oProcesser->releaseProcess();
    }
}

//ˢ��������ͼ��
void fn_refreshTaskbarIcon()
{
    //����������
    HWND hShellTrayWnd = ::FindWindow("Shell_TrayWnd", NULL);
    //�������ұ�����ͼ��+ʱ����
    HWND hTrayNotifyWnd = ::FindWindowEx(hShellTrayWnd, 0, "TrayNotifyWnd", NULL);
    //��ͬϵͳ�����п���û�����
    HWND hSysPager = ::FindWindowEx(hTrayNotifyWnd, 0, "SysPager", NULL);
    //����ͼ�괰��
    HWND hToolbarWindow32;
    if (hSysPager)
    {
        hToolbarWindow32 = ::FindWindowEx(hSysPager, 0, "ToolbarWindow32", NULL);
    }
    else
    {
        hToolbarWindow32 = ::FindWindowEx(hTrayNotifyWnd, 0, "ToolbarWindow32", NULL);
    }
    if (hToolbarWindow32)
    {
        RECT r;
        ::GetWindowRect(hToolbarWindow32, &r);
        int width = r.right - r.left;
        int height = r.bottom - r.top;
        //���������м������,���ϵ���, MOUSEMOVEһ�飬����ͼ��״̬�ᱻ����
        for (int x = 1; x < width; x++)
        {
            for (int y = 1; y < height; y++)
            {
                ::SendMessage(hToolbarWindow32, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));  // ����WM_MOUSEMOVE��Ϣ
            }
        }
    }
}
//ˢ������������ͼ�� Win7ϵͳ����ϵͳ
void fn_refreshTaskbarIconHide()
{
    //���������ش���
    HWND hShellTrayWnd = ::FindWindow("NotifyIconOverflowWindow", NULL);
    //����ͼ�괰��
    HWND hToolbarWindow32 = NULL;
    if (hShellTrayWnd)
    {
        hToolbarWindow32 = ::FindWindowEx(hShellTrayWnd, 0, "ToolbarWindow32", NULL);
    }
    if (hToolbarWindow32)
    {
        RECT r;
        ::GetWindowRect(hToolbarWindow32, &r);
        int width = r.right - r.left;
        int height = r.bottom - r.top;
        //���������м������,���ϵ���, MOUSEMOVEһ�飬����ͼ��״̬�ᱻ����
        for (int x = 1; x < width; x++)
        {
            for (int y = 1; y < height; y++)
            {
                ::SendMessage(hToolbarWindow32, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));  // ����WM_MOUSEMOVE��Ϣ
            }
        }
    }
}

//ˢ��������ͼ��
void fn_refreshTaskbarIcon2()
{
    HWND hWnd, hWndPaper;
    unsigned long lngPID;
    long ret, lngButtons;
    HANDLE hProcess;
    LPVOID lngAddress;
    long lngTextAdr, lngHwndAdr, lngHwnd, lngButtonID;
    char strBuff[1024] = {0};
    char *str = NULL;
    char *pp = NULL;

    hWnd = ::FindWindow("Shell_TrayWnd", NULL);
    hWnd = ::FindWindowEx(hWnd, 0, "TrayNotifyWnd", NULL);
    hWndPaper = ::FindWindowEx(hWnd, 0, "SysPager", NULL);
    if (!hWndPaper)
        hWnd = ::FindWindowEx(hWnd, 0, "ToolbarWindow32", NULL);
    else
        hWnd = ::FindWindowEx(hWndPaper, 0, "ToolbarWindow32", NULL);

    //    ret = GetWindowThreadProcessId( hWnd, &lngPID );
    //    hProcess = OpenProcess( PROCESS_ALL_ACCESS
    //                            |PROCESS_VM_OPERATION
    //                            |PROCESS_VM_READ
    //                            |PROCESS_VM_WRITE,
    //                            0,
    //                            lngPID );

    //    lngAddress = VirtualAllocEx( hProcess,0, 0x4096, MEM_COMMIT, PAGE_READWRITE );
    //    lngButtons = ::SendMessage( hWnd, TB_BUTTONCOUNT, 0, 0 );
    //    for( int i=0; i< lngButtons - 1; i++ )
    //    {
    //        ret = ::SendMessage( hWnd, TB_GETBUTTON, i, long(lngAddress) );
    //        ret = ReadProcessMemory( hProcess, LPVOID(long(lngAddress) + 16), &lngTextAdr, 4, 0 );
    //        if( lngTextAdr != -1 )
    //        {
    //            ret = ReadProcessMemory( hProcess, LPVOID(lngTextAdr), strBuff, 1024, 0 );
    //            ret = ReadProcessMemory( hProcess, LPVOID(long(lngAddress) + 12), &lngHwndAdr, 4, 0 );
    //            ret = ReadProcessMemory( hProcess, LPVOID(lngHwndAdr),&lngHwnd, 4, 0 );
    //            ret = ReadProcessMemory( hProcess, LPVOID(long(lngAddress) + 4), &lngButtonID, 4, 0 );
    //            ::SendMessage( hWnd, TB_HIDEBUTTON, lngButtonID, 1 );

    //            USES_CONVERSION;
    //            str = OLE2T( (LPOLESTR)( strBuff ) );
    //            pp=strstr( str,"��������" ); // ͨ�������������ȶ�
    //            if(pp != NULL)
    //            {
    //                ::SendMessage( hWnd, TB_HIDEBUTTON, lngButtonID, 1 );
    //            }
    //        }
    //    }
    //    VirtualFreeEx( hProcess, lngAddress, 0X4096, MEM_RELEASE );
    //    CloseHandle( hProcess );
}

void fn_refreshOnlineProcessInfos()
{
    _onlineProcessInfos = CxProcess::getRunningProcesses();
    for(std::vector<CxProcess::ProcessInfo>::iterator it = _onlineProcessInfos.begin(); it != _onlineProcessInfos.end();)
    {
        const CxProcess::ProcessInfo & processInfo = * it;
        Processer * oProcesser  = fn_findPerformanceProcess(processInfo.name);
        if (! oProcesser)
        {
            it = _onlineProcessInfos.erase(it);
        }
        else
        {
            ++it;
        }
    }
    DaemonGlobal::fillCommandLine(_onlineProcessInfos);
}

void fn_refreshPerformanceProcessesPid()
{
    fn_refreshOnlineProcessInfos();
    for (size_t i = 0; i < f_oPerformanceProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oPerformanceProcesses.at(i);
        const CxProcess::ProcessInfo * oProcessInfo = DaemonGlobal::findProcessInfo(_onlineProcessInfos, & oProcesser->mProcessConfig);
        if (oProcessInfo)
        {
            oProcesser->mPid = oProcessInfo->pid;
        }
        else
        {
            PROCESS_INFORMATION pid;
            memset(&pid, 0, sizeof(pid));
            oProcesser->mPid = pid;
        }
    }
}

//main
int main(int argc, const char *argv[])
{
    string sApplicationPath = argc > 0 ? CxFileSystem::extractPath(argv[0]) : string();
    if (!CxFileSystem::hasRootPath(sApplicationPath)) sApplicationPath = CxFileSystem::getCurrentDir();
    if (CxFileSystem::hasRootPath(sApplicationPath))
    {
        CxAppEnv::setApplicationDefaultDeployPath(CxFileSystem::extractPath(argv[0]));
    }
    CxAppEnv::init(argc, argv);

    string sApplicationFilePath = CxAppEnv::applicationFilePath();

    gs_defaultBinPath = CxAppEnv::applicationPath();

    if (argc > 0)
    {
        string sName = CxAppEnv::applicationTargetName();
        HANDLE m_hMutex = ::CreateMutex(NULL, TRUE, sName.c_str());
        int r = ::GetLastError();
        cout << "GetLastError:" << r << endl;
        if (r == ERROR_ALREADY_EXISTS)
        {
            string sInfo = string("�Ѿ���һ��ʵ������������:") + sApplicationFilePath;
            ::MessageBox(NULL, sInfo.c_str(), "�澯", MB_OKCANCEL);
            ::CloseHandle(m_hMutex);
            m_hMutex = NULL;
            return 0;
        }
    }

    fn_outInfo(CxAppEnv::applicationTargetName() + " Run Begin:");
    fn_outInfo("ApplicationFilePath:" + sApplicationFilePath);

    fn_void_int_t prev_handler = signal(SIGINT, fn_handler);
    if (prev_handler == SIG_ERR)
    {
        fn_outInfo("Error : signal (SIGINT , *)");
    }

    HWND hWnd = ::FindWindow(NULL, sApplicationFilePath.c_str());

    ::ShowWindow(hWnd, SW_SHOWMINIMIZED);

    string sShow = CxString::toLower(CxAppEnv::findArgument(string("show")));
    if (sShow == "hide")
    {
        ::ShowWindow(hWnd, SW_HIDE);
    }

    fn_loadProcessers();

    if (f_oDaemonProcesses.empty())
    {
        fn_outInfo("û��Ҫ�ػ���Ӧ�ó��򣬼�����������ȷ��!!!");
        ::system("pause");
        return 0;
    }

    fn_outInfo(string("daemon start time : ") + CxString::toString(CxTime::currentSystemTime()));

    WSADATA wsd;            //WSADATA����
    //��ʼ���׽��ֶ�̬��
    if (::WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
    {
        fn_outInfo("��ʼ���׽��ֶ�̬��ʧ��!!!");
        ::system("pause");
        return 0;
    }

    for (size_t i = 0; i < f_oDaemonProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oDaemonProcesses.at(i);
        int ret = CxProcess::kill(oProcesser->mProcessConfig.exeFileName);
        fn_outInfo(string("kill [") + CxString::toString(ret) + "] ���� :" + oProcesser->mProcessConfig.exeFileName);
    }

    for (size_t i = 0; i < f_oDaemonProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oDaemonProcesses.at(i);
        oProcesser->createProcess();
        if (oProcesser->mProcessConfig.localPort > 0 && oProcesser->mProcessConfig.localPort < USHRT_MAX)
        {
            oProcesser->createSocket();
        }
        if (oProcesser->mProcessConfig.waitFirstMillisecond > 0)
        {
            ::Sleep(oProcesser->mProcessConfig.waitFirstMillisecond);
            fn_outInfo(string("���� : ") + oProcesser->mProcessConfig.exeFileName + "�״������ȴ��೤ʱ����������һ��������");
        }
    }

    // �Ȼ�ȡϵͳCPU��ʱ�䣬�ٸ�����ʱ����������CPUռ����
    Processer::updateSysInfo();
    cout << "����ϵͳ��Ϣ: sysCpu=" << (int) Processer::s_SysCpuRatio << "% sysMemory:" << (int) Processer::s_SysMemeryRatio << "%" << endl;
    for (size_t i = 0; i < f_oDaemonProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oDaemonProcesses.at(i);
        oProcesser->updateProcessInfo();
        cout << i << "pid:" << oProcesser->mPid.dwProcessId << "\tcpu:" << (int) oProcesser->mCpuRatio << "%\tmem:"
             << oProcesser->mMemerySize / 1024 << "k\t\t:" << oProcesser->mProcessConfig.exeFileName
             << oProcesser->mIsThreadWaitFinish
             << endl;

    }
    for (size_t i = 0; i < f_oPerformanceProcesses.size(); ++i)
    {
        Processer *oProcesser = f_oPerformanceProcesses.at(i);
        oProcesser->updateProcessInfo();
        cout << i << "pid:" << oProcesser->mPid.dwProcessId << "\tcpu:" << (int) oProcesser->mCpuRatio << "%\tmem:"
             << oProcesser->mMemerySize / 1024 << "k\t\t:" << oProcesser->mProcessConfig.exeFileName
             << oProcesser->mIsThreadWaitFinish
             << endl;

    }

    msepoch_t dtPreNow = CxTime::currentSystemTime();
    unsigned int iMsgLong = 0;
    unsigned int iUpdateLong = 0;
    int iMainState = 1;

    while (iMainState > 0)
    {
        msepoch_t dtNow = CxTime::currentSystemTime();
        unsigned int dwSpan = dtNow - dtPreNow;
        string sMsg;
        for (size_t i = 0; i < f_oDaemonProcesses.size(); ++i)
        {
            Processer *oProcesser = f_oDaemonProcesses.at(i);
            //1)exe�Ѿ��˳�;
            if (oProcesser->mIsThreadWaitFinish && CxTime::milliSecondDifferToNow(oProcesser->mStartTime) > 5000)
            {
                if (CxProcess::isRunning(oProcesser->mPid))
                {
                    if (CxTime::currentSystemTime() - oProcesser->mIsThreadWaitFinishOutInfoTime > 0)
                    {
                        fn_outInfo(string("Warn : ���� ") + oProcesser->mProcessConfig.exeFileName + " IsThreadWaitFinish, But IsRunnig!!! CommandLine: " + oProcesser->mProcessConfig.commandLine);
                        oProcesser->mIsThreadWaitFinishOutInfoTime+=5000;
                    }
                }
                else
                {
                    oProcesser->rerunProcess();
                }
                continue;
            }//2)�����ݳ�ʱ
            else if ((oProcesser->mProcessConfig.heartBuffer.size() > 0 && oProcesser->mLastReceiveTime > 0
                      && ((CxTime::currentSystemTime() - oProcesser->mLastReceiveTime)
                          > oProcesser->mProcessConfig.timeOut_receive)))
            {
                //*
                ProcessAlarm processAlarm;
                processAlarm.aid = oProcesser->mProcessConfig.aid;
                processAlarm.ord = oProcesser->mProcessConfig.ord;
                processAlarm.process = oProcesser->mProcessConfig.exeFileName;
                processAlarm.alarmTime = CxTime::currentSystemTime();
                processAlarm.alarmType = ProcessAlarm::AlarmType_HeartBeatTimeout;
                processAlarm.alarmMsg = "HeartBeat Timeout";
                DaemonGlobal::log(ProcessAlarm::getLogClass(), processAlarm.toJson());
                //*
                oProcesser->rerunProcess();
                continue;
            }

            //socket����������������
            if (oProcesser->mIsThreadReceiveError)
            {
                oProcesser->restartSocket();
                continue;
            }

            if (iMsgLong > 1000 * 60)
            {
                int dSum = (int) ((dtNow - oProcesser->mStartTime) / 1000);
                int day = dSum / 86400;
                int hour = dSum % 86400 / 3600;
                int min = dSum / 60 % 60;
                sMsg += CxString::format("��%d�� ��������: %s ; �����˿�: %d ;��������: %s \n", i
                                                                                  + 1, oProcesser->mProcessConfig.exeFileName.c_str(), oProcesser->mProcessConfig.localPort, oProcesser->mProcessConfig.heartHexString.c_str());
                sMsg += CxString::format("1) ����ʱ��: %04d-%02d:%02d; ����: %s; ����: %d; �ۻ�����: %lld\n", day, hour, min, CxTime::toString(oProcesser->mStartTime).c_str(), oProcesser->mStartTimes, oProcesser->mReceiveBytes);
                sMsg += CxString::format("2) ���̱�־: %d; �ϴ�����: %lld��; ����ʱ��: %s\n\n", (int) oProcesser->mIsThreadWaitFinish,
                                         (CxTime::currentSystemTime() - oProcesser->mLastReceiveTime)
                                         / 1000, CxTime::toString(oProcesser->mLastReceiveTime).c_str());
            }
        }

        if (iMsgLong > 1000 * 60)
        {
            sMsg =
                CxString::format("��ǰʱ��:%s,�ػ�%d��Ӧ�ó���,�����Ǹ�Ӧ�ó������Ϣ��\n\n", CxTime::toString(dtNow).c_str(), f_oDaemonProcesses.size())
                + sMsg;
            fn_outInfo(sMsg);

            iMsgLong = 0;

            //Check Want Kill ÿ��60s
            for (size_t i = 0; i < f_oKillProcesses.size(); ++i)
            {
                const string &sKillProcess = f_oKillProcesses.at(i);
                int ret = CxProcess::kill(sKillProcess);
                if (ret > 0) fn_outInfo(string("kill [") + CxString::toString(ret) + "] ���� :" + sKillProcess);
            }

            sMsg = "ˢ��������״̬...\n";
            fn_outInfo(sMsg);
            fn_refreshTaskbarIcon();
            fn_refreshTaskbarIconHide();
        }

        if (iUpdateLong > 1000 * 3)
        {
            Processer::updateSysInfo();
            cout << "����ϵͳ��Ϣ: sysCpu=" << (int) Processer::s_SysCpuRatio << "% sysMemory:" << (int) Processer::s_SysMemeryRatio << "%"
                 << endl;
            for (size_t i = 0; i < f_oDaemonProcesses.size(); ++i)
            {
                Processer *oProcesser = f_oDaemonProcesses.at(i);
                oProcesser->updateAndLog();
            }
            fn_refreshPerformanceProcessesPid();
            for (size_t i = 0; i < f_oPerformanceProcesses.size(); ++i)
            {
                Processer *oProcesser = f_oPerformanceProcesses.at(i);
                oProcesser->updateAndLog();
            }
            iUpdateLong = 0;
        }

        iMsgLong += dwSpan;
        iUpdateLong += dwSpan;
        dtPreNow = dtNow;
        ::Sleep(100);
    }

    fn_outInfo(CxAppEnv::applicationTargetName() + " Run End.");

    ::WSACleanup();        //�ͷ��׽�����Դ

    ::exit(0);

    return 0;
}


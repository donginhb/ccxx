
set sExecDisk=%~d0%
set sExecPath=%~dp0%\
set sExecPath=%sExecPath:\\=\%

#sPathYgct_rtdb ��RTDB��·��
#sPathYgct_104_v100 ��Ygct_104_v100��·��

set sPathYgct_rtdb=%sExecPath%ygct_rtdb\ygct_rtdb.exe
set sPathYgct_104_v100=ygct_104\ygct_104_v100.exe

echo  "sExecPath="%sExecPath%
echo  "sPathYgct_rtdb="%sPathYgct_rtdb%
echo  "sPathYgct_104_v100="%sPathYgct_104_v100%

cd /d %sExecPath%

#�ô��������ػ�����������ص�Ӧ�ó���
#֧�ֶ��Ӧ�ó��򣬶��Ӧ�ó���֮���÷ֺŸ���
#һ��Ӧ�ó����Ӧһ��UDP�ļ����˿�


###
#sample
#-config pf=bbb/bbb.exe;pf=aaa/aaa.exe,pt=1,pw=3000,np=5566,nt=6600,nh=a55aa55a
###
#
#pf [process file]��Ӧ�ó����ļ�������д���·�������·��
#pt [process type]��Ӧ�ó�������32λ�����λ��ʾ�Ƿ���ĳ�������Ǻ��ģ��˳���һ��over�ͱ���ȫ��Ӧ�ó�����������
#pw [process wait]��Ӧ�ó����״������ȴ��೤ʱ����������һ�� --{Ĭ��Ϊ�������̺󲻵ȴ�}
#np [network port]����������˿ں��б����� --{Ĭ��Ϊ�գ������������ж�}
#nt [network timeout]�����糬ʱ��û���յ����ݵĳ�ʱ --{Ĭ��Ϊ"5000"}
#nh [network heart]�������������� --{Ĭ��Ϊ"a55aa55a"}
#pp [process param]���̵Ĳ���
#ע�� : ���Ӧ�ó���֮���÷ָ���';',��������ΪĬ��ֵ
#-config pf=%sPathYgct_rtdb%;pf=%sPathYgct_104_v100%,pt=1,pw=3000,np=5566,nt=6600,nh=a55aa55a
start %sExecPath%gcdaemon.exe -config pf=%sPathYgct_104_v100%,pt=1,pw=3000,np=5566,nt=6600,nh=a55aa55a;pf=%sPathYgct_rtdb%


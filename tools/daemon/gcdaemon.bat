
set sExecDisk=%~d0%
set sExecPath=%~dp0%\
set sExecPath=%sExecPath:\\=\%

#sPathPc �ǹ�Լת������·��
#sPathTs ��Ʊ�����·��

set sPathPc=%sExecPath%ProtocolConvertor\ProtocolConvertor.exe
set sPathTs=%sExecPath%TicketServer\TicketServer.exe

echo  "sExecPath="%sExecPath%
echo  "sPathPc="%sPathPc%
echo  "sPathTs="%sPathTs%

cd /d %sExecPath%

#�ô��������ػ�����������ص�Ӧ�ó���
#֧�ֶ��Ӧ�ó��򣬶��Ӧ�ó���֮���÷ֺŸ���
#һ��Ӧ�ó����Ӧһ��UDP�ļ����˿�
#-p [process]��Ӧ�ó����б�
#-n �Ǽ����˿ں��б�����
#-t [timeout]��û���յ����ݵĳ�ʱ
#-h [heart]����������
#ע�� : Ӧ�ó���������˿���һ��һ�Ķ�Ӧ��ϵ
start %sExecPath%gcdaemon.exe -t 6000 -p %sPathPc%;%sPathTs% -n 5561;5562 -h a55aa55a;a55aa55a
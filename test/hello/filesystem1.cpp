
#undef UNICODE

#include<windows.h>
#include<iostream>
#include<string>

using namespace std;

//ֻ�ܴ���Ŀ¼:lpPathֻ����·��
void find(char *lpPath)
{
    char szFind[MAX_PATH];
    char szFile[MAX_PATH];

    WIN32_FIND_DATA FindFileData;

    strcpy(szFind,lpPath);
    strcat(szFind,"//*.*");

    HANDLE hFind=::FindFirstFile(szFind,&FindFileData);
    if(INVALID_HANDLE_VALUE == hFind)    return;
    while(TRUE)
    {
        if(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if(FindFileData.cFileName[0]!='.')
            {
                strcpy(szFile,lpPath);
                strcat(szFile,"//");
                strcat(szFile,FindFileData.cFileName);
                find(szFile);
            }
        }
        else
        {
            cout<<FindFileData.cFileName<<endl;
        }
        if(!FindNextFile(hFind,&FindFileData))
            break;
    }
    FindClose(hFind);
}
//��ͬʱ����Ŀ¼���ļ�:path������·����Ҳ�������ļ����������ļ�ͨ���
void _find(string path)
{
    //ȡ·�������һ��"//"֮ǰ�Ĳ���,����"//"
    string prefix=path.substr(0,path.find_last_of('//')+1);

    WIN32_FIND_DATA FindFileData;
    HANDLE hFind=::FindFirstFile(path.c_str(),&FindFileData);
    if(INVALID_HANDLE_VALUE == hFind)
    {
        cout<<"�ļ�ͨ�������"<<endl;
        return;
    }
    while(TRUE)
    {
        //Ŀ¼
        if(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            //���ǵ�ǰĿ¼��Ҳ���Ǹ�Ŀ¼
            if(FindFileData.cFileName[0]!='.')
            {
                //������һ��Ŀ¼
                _find(prefix+FindFileData.cFileName+"//"+"*.*");
            }
        }
        //�ļ�
        else
        {
            cout<<FindFileData.cFileName<<endl;
        }
        if(!FindNextFile(hFind,&FindFileData))
            break;
    }
    FindClose(hFind);
}

int main()
{
    find("c:/test");//Ŀ¼��E��
//    _fi0nd("c:/test//*.*");//E���������ļ�

    return 0;
    string str=".//";
    string path;
    cout<<"�������ļ�ͨ�����"<<flush;
    cin>>path;
    str=str+path;
    find((char*)str.c_str());//���Դ���"."��".." �����Դ���"*"��"..//*"
    _find(str);//���Դ���"*"��"..//*" �����Դ���"."��".."
    return 0;
}


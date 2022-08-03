#include "UpLoadManager.h"
#include <stdio.h>
#include <iostream>
using namespace std;

bool UploadManager::CreateFile(const char* data,size_t len,const string& file_name)
{
	string file_full_name=upload_file_root_path+file_name;
	FILE *fd=fopen(file_full_name.c_str(),"w+");
	if(fd==nullptr)
		return false;
	size_t ret=fwrite(data,len,1,fd);
	if(ret==0)
		return false;

	ret=fflush(fd);
	if(ret!=0)
		return false;

	ret=fclose(fd);
	if(ret!=0)
		return false;

	return true;
}


string UploadManager::UploadFile(const string& body,const string& boundary)
{
	//返回值为vector<string>,用来返回文件上传的结果
		string res="";

		string boundary_="--"+boundary;
		string file_name;
		int file_name_pos;
		int content_type_pos;
		int data_pos;

		int cur_pos=body.find(boundary_,boundary_.size());
		int nxt_pos=body.find(boundary_,cur_pos+boundary_.size());
		bool ret;

		while(nxt_pos!=string::npos)
		{
			//查找位置
			file_name_pos=body.find("filename=",cur_pos+boundary_.size());
			content_type_pos=body.find("Content-Type: ",file_name_pos);
			data_pos=body.find("\r\n",content_type_pos);
			data_pos+=4;

			file_name=body.substr(file_name_pos+10,content_type_pos-file_name_pos-13);

			//在本地创建文件
			ret=CreateFile(body.c_str()+data_pos,nxt_pos-data_pos,file_name);
			if(!ret)
            {
                res+=file_name;
                res+=":upload failed\n";
            }

			//切换到下一块
			cur_pos=nxt_pos;
			nxt_pos=body.find(boundary_,cur_pos+boundary_.size());
		}

		if(res.size()==0)
			res="upload all success!\n";

		return res;
}

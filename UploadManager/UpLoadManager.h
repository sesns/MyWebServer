#ifndef UPLOADMANAGER_H_INCLUDED
#define UPLOADMANAGER_H_INCLUDED
#include <string>
#include <vector>
using namespace std;

const string upload_file_root_path="/home/moocos/CodeBlockWebServer/WebServer/files_uploaded/";


class UploadManager
{
private:
	bool CreateFile(const char* data,size_t len,const string& file_name);
public:
	UploadManager()
	{

	}

	~UploadManager()
	{

	}

	string UploadFile(const string& body,const string& boundary);
};

#endif // UPLOADMANAGER_H_INCLUDED

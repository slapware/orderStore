// epubStore.cpp : Defines the entry point for the console application.
//

#define BUFSIZE 1024

#if defined(WIN32) || defined (_WIN64)
#define PATH_SEPARATOR  "\\"
#include "Windows.h"
#include <direct.h>
#define GetCurrentDir _getcwd 
#else
#define PATH_SEPARATOR  "/"
#include <unistd.h> 
#define GetCurrentDir getcwd 
#endif
///////////////////////////////
//for debugging system only//
/////////////////////////////
//#define _VERBOSE 1
#ifndef WIN32
#define _USE_OPENSSL 1
#endif
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string.hpp> // for boost trim()
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/regex.hpp>


#ifndef (WIN32) || defined (_WIN64)
#include <openssl/md5.h>
#endif


#include "Markup.h"
// MySql headers
#include "mysql_connection.h"

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
// ChilKat headers
#include <CkZip.h>
#include <CkXml.h>
#include <CkZipEntry.h>
#include <CkByteData.h>

//////////////////////////////
//Required namespace section//
//////////////////////////////
using namespace std;
using namespace boost;
using namespace boost::filesystem;
using namespace boost::algorithm;
using namespace boost::gregorian;
using namespace boost::posix_time;
using namespace boost::local_time;
//////////////////////////////////
//Forward declarations are here.//
//////////////////////////////////
std::string NewIdent(std::string pFile);
void LogError(string lFile, string pMsg);
#ifndef WIN32
std::string getMd5(std::string pFile);
#endif
int regexSearch(const string st);
unsigned int wordcount(const std::string& str);

typedef map<string, string, less<string> > mimeMap;
typedef mimeMap::value_type mime_entry;

int main(int argc, char* argv[])
{
	path pth;
	std::string m_thr_error;
	CkZip zip;
	unsigned int m_runErrors, m_filesChecked, m_notFound;
	std::ostringstream m_strstrm;
	if (argc == 2)
	{
		pth = argv[1];
	}
	else
	{
		cout << "No Path provided!?" << endl;
		return 1;
	}

	mimeMap m_mimeMap;

	typedef vector<path> vec;             // store paths,
	vec v;                             // so we can sort them later
	v.reserve(10000);
	copy(directory_iterator(pth), directory_iterator(), back_inserter(v));

	char cCurrentPath[BUFSIZE];
	if (!GetCurrentDir(cCurrentPath, sizeof(cCurrentPath))) 
	{ 
		return false; 
	}
	///////////////////////////////////////////////////////////
	//Select the name of .config file to load for DB connect.//
	///////////////////////////////////////////////////////////
	std::string pXmlSetting = cCurrentPath;
	std::string m_currentPath = cCurrentPath;
	std::string m_zipkey;
	std::string dbserver, dbuser, dbpass, dbstore, m_log;
	pXmlSetting += PATH_SEPARATOR;
	pXmlSetting += "epubStore.xml";
	{ // scope the life of CMarkup
		CMarkup m_xml;
		if (!m_xml.Load(pXmlSetting.c_str() ) )
		{
			std::cout << "Failed to locate and load XML configuration." << endl;
			return false;
		} 
		else
		{
			m_xml.FindElem();
			m_xml.IntoElem();

			m_xml.FindElem("database");
			m_xml.FindChildElem("server");
			dbserver = (m_xml.GetChildData());
			m_xml.FindChildElem("user");
			dbuser = (m_xml.GetChildData());
			m_xml.FindChildElem("pass");
			dbpass = (m_xml.GetChildData());
			m_xml.FindChildElem("store");
			dbstore = (m_xml.GetChildData());
			m_xml.FindChildElem("log");
			m_log = m_currentPath += PATH_SEPARATOR;
			m_log += (m_xml.GetChildData());
			m_xml.FindChildElem("zipkey");
			m_zipkey = (m_xml.GetChildData());


		}

	} // end scope life of CMarkup

	bool success;
	success = zip.UnlockComponent(m_zipkey.c_str());
	if (success != true) 
	{
		m_thr_error = zip.lastErrorText();
		LogError(m_log, m_thr_error);
#ifdef 	_VERBOSE
	cout << "Zip  component not active, bailing out" << m_currentPath << endl;
	cout << "Log file is " << m_log << endl;
#endif	
		return 1;
	}
	
	string iamawake = "Starting run";
	LogError(m_log, iamawake);
	m_runErrors = 0;
	m_filesChecked = 0;

#ifdef 	_VERBOSE
	cout << "Current directory is " << m_currentPath << endl;
	cout << "Log file is " << m_log << endl;
#endif	

	sql::Driver *driver;
	sql::Connection *conn;
	sql::ResultSet *res;

	// -------------------------------------------------------------------------------
	//  MySql connection for thread --- 
	// -------------------------------------------------------------------------------
		try {
			driver = get_driver_instance();
			conn = driver->connect(dbserver.c_str(), dbuser.c_str(), dbpass.c_str());
			conn->setSchema(dbstore.c_str());
		}
		catch (sql::SQLException x)
		{
			string error = x.what();
			error += "No DB Error: " + error;
			LogError(m_log, error);
			return 1;
		}
	// -------------------------------------------------------------------------------
	//  vector of media types to determine if blob of long text --- 
	// -------------------------------------------------------------------------------
	vector<std::string> blobTypes;
	blobTypes.push_back(".m4a");
	blobTypes.push_back(".mpg");
	blobTypes.push_back(".mp3");
	blobTypes.push_back(".m4v");
	blobTypes.push_back(".ogg");
	blobTypes.push_back(".mov");
	blobTypes.push_back(".wav");
	// done multimedia, now images
	blobTypes.push_back(".png");
	blobTypes.push_back(".jpg");
	blobTypes.push_back(".gif");
	blobTypes.push_back(".bmp");
	blobTypes.push_back(".jpeg");
	// font file types
	blobTypes.push_back(".otf");
	blobTypes.push_back(".ttf");
// -------------------------------------------------------------------------------
//  m_mimeMap to determine mime type by extension lookup --- 
// -------------------------------------------------------------------------------
	m_mimeMap.insert(mime_entry(".opf", "application/oebps-package+xml"));
	m_mimeMap.insert(mime_entry(".xhtml", "application/xhtml+xml"));
	m_mimeMap.insert(mime_entry(".ncx", "application/x-dtbncx+xml"));
	m_mimeMap.insert(mime_entry(".css", "text/css"));
	m_mimeMap.insert(mime_entry(".html", "application/xhtml+xml"));
	m_mimeMap.insert(mime_entry(".htm", "text/html"));
	m_mimeMap.insert(mime_entry(".m4a", "audio/m4a"));
	m_mimeMap.insert(mime_entry(".wav", "audio/x-wav"));
	m_mimeMap.insert(mime_entry(".xpgt", "application/vnd.adobe-page-template+xml"));
	m_mimeMap.insert(mime_entry(".jpg", "image/jpeg"));
	m_mimeMap.insert(mime_entry(".jpeg", "image/jpeg"));
	m_mimeMap.insert(mime_entry(".png", "image/png"));
	m_mimeMap.insert(mime_entry(".ogg", "video/ogg"));
	m_mimeMap.insert(mime_entry(".mov", "video/quicktime"));
	m_mimeMap.insert(mime_entry(".mp3", "video/mpeg3"));
	m_mimeMap.insert(mime_entry(".m4v", "video/mp4"));
	m_mimeMap.insert(mime_entry(".gif", "image/gif"));
	m_mimeMap.insert(mime_entry(".bmp", "image/bmp"));
	m_mimeMap.insert(mime_entry(".otf", "application/vnd.ms-opentype"));
	m_mimeMap.insert(mime_entry(".xml", "application/xml"));
	m_mimeMap.insert(mime_entry(".ttf", "application/x-font-ttf"));

	bool didFind = false;
	bool isBlob = false;
	for (vec::const_iterator it(v.begin()), it_end(v.end()); it != it_end; ++it)
	{
		std::string pathname = (*it).string();
		std::string m_fname;
		std::string m_shortName;
		std::string m_md5;
		unsigned long m_lastRow = 0;
		size_t ss = pathname.find_last_of(PATH_SEPARATOR);
		if (ss != string::npos)
		{
			m_shortName = pathname;
			m_shortName.erase(0, ss + 1);
		} 
		else
		{
			m_shortName = pathname;
		}
	#ifdef 	_VERBOSE
		cout << "in pathname is " << pathname << endl;
		cout << "short pathname is " << m_shortName << endl;
	#endif

		try
		{
			boost::shared_ptr<sql::Statement> fcOnnstmt(conn->createStatement());
			fcOnnstmt->execute("SET FOREIGN_KEY_CHECKS=0");
		}
		catch (sql::SQLException x)
		{
			string error = x.what();
			error += " SET FOREIGN_KEY_CHECKS = 0";
			LogError(m_log, error);
		}

		if (pathname.find(".epub") != string::npos)
		{
#ifdef _USE_OPENSSL
			m_md5 = getMd5(pathname);
//			cout << "md5 = " << m_md5 << endl;
#endif
			success = zip.OpenZip(pathname.c_str());
			if (success != true) 
			{
				m_thr_error = zip.lastErrorText();
				continue;;
			}
			std::string m_realIsbn = NewIdent(m_shortName);
			long long m_longIsbn;
			m_longIsbn = boost::lexical_cast<long long>(m_realIsbn.c_str());
			CkZipEntry *entry = 0;
			string::size_type pos = 0;
			int zcnt = zip.get_NumEntries();
			int i;
			didFind = false;
			unsigned int m_wordcount = 0;
			// -------------------------------------------------------------------------------
			//  The iterate zip loop via entry index --- 
			// -------------------------------------------------------------------------------
			for (i = 0; i <= zcnt - 1; i++)
			{
				isBlob = false;
				CkString sFilename;
				CkString sMemFile;
				entry = zip.GetEntryByIndex(i);
				if(entry->get_IsDirectory())
					continue;

				entry->get_FileName(sFilename);
				std::string inputFile = sFilename.getAnsi();
				vector<std::string>::const_iterator blobTypeItr;
				// -------------------------------------------------------------------------------
				//  Check if blob via mimeMap lookup --- 
				// -------------------------------------------------------------------------------				  
				for (blobTypeItr = blobTypes.begin(); blobTypeItr != blobTypes.end(); blobTypeItr++)
				{
					if(icontains(inputFile, *blobTypeItr))
					{
						isBlob = true;
					}
				}
				if(isBlob)
				{
					CkByteData imageData;
					entry->Inflate(imageData);
					std::string tempDir;
#if defined(WIN32) || defined (_WIN64)
					DWORD dwRetVal, dwBufSize;
					dwBufSize = BUFSIZE;
					char buffer[BUFSIZE];
					dwRetVal = GetTempPath(dwBufSize, buffer);
					tempDir = buffer;
#else
					tempDir = "/tmp/";

#endif
					entry->Extract(tempDir.data());
					std::string target = tempDir + inputFile;
					boost::scoped_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("INSERT INTO epub2 (edition_id, md5, filename, multi) VALUES(?,?,?,?)"));
#ifdef 	_VERBOSE
					cout << "target " << target << endl;
					cout << "inputFile is " << inputFile << endl;
#endif	

					std::ifstream fp;
					try
					{
						fp.open(target.data(), ios::in | ios::binary);
						if (!fp.is_open())
						{
							string error = "The file did not open. ";
							error += target;
							LogError(m_log, error);
						}
						stmt->setInt64(1, m_longIsbn);
						stmt->setString(2, m_md5.c_str());
						stmt->setString(3, inputFile.c_str());
						stmt->setBlob(4, &fp);
					}
					catch (sql::SQLException x)
					{
						m_strstrm << x.what() << " while inserting LONGBLOB ";
						m_strstrm << inputFile << " in " << m_realIsbn;
						m_strstrm << " current File count = " << m_filesChecked + 1;
						string error = m_strstrm.str();
						m_strstrm.str("");

						LogError(m_log, error);
						m_runErrors++;
					}
					try
					{
						stmt->execute();
						boost::scoped_ptr<sql::Statement> qstmt(conn->createStatement());
						res = qstmt->executeQuery("SELECT @@identity AS id");
						res->next();
						m_lastRow = res->getUInt("id");
						fp.close();
						remove(target.data());
					}
					catch (sql::SQLException x)
					{
						m_strstrm << x.what() << " getting identity for ";
						m_strstrm << inputFile << " in " << m_realIsbn;
						m_strstrm << " current File count = " << m_filesChecked + 1;
						string error = m_strstrm.str();
						m_strstrm.str("");

						LogError(m_log, error);
						m_runErrors++;
					}
				} // isBlob == TRUE
				else // LONGTEXT data to insert
				{
					entry->get_FileName(sFilename);
					std::string inputFile = sFilename.getAnsi();
					sMemFile.clear();
					if(entry->UnzipToString(0, "utf-8", sMemFile))
					{
						std::string stdMem = sMemFile.getUtf8();
						trim(stdMem);
						if((inputFile.find(".htm") != string::npos) || (inputFile.find(".xhtm") != string::npos))
						{
							// NOTE: clean the string.
							basic_string <char>::size_type lastchar = 0;
							lastchar = stdMem.find_last_of(">");
							if(lastchar != stdMem.length() - 1)
							{
								stdMem.erase(lastchar +1, stdMem.length() - lastchar);
							}
						}

						if (inputFile.find(".css") != string::npos)
						{
							// NOTE: clean the string.
							basic_string <char>::size_type lastchar = 0;
							lastchar = stdMem.find_last_of("}");
							if(lastchar != stdMem.length() - 1)
							{
								stdMem.erase(lastchar +1, stdMem.length() - lastchar);
							}
						}
						istringstream iss(stdMem);
						boost::scoped_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("INSERT INTO epub2 (edition_id, md5, filename, meta) VALUES(?,?,?,?)"));
						try
						{
							stmt->setInt64(1, m_longIsbn);
							stmt->setString(2, m_md5.c_str());
							stmt->setString(3, inputFile.c_str());
							stmt->setBlob(4, &iss);
						}
						catch (sql::SQLException x)
						{
							m_strstrm << x.what() << " while inserting CLOB ";
							m_strstrm << inputFile << " in " << m_realIsbn;
							m_strstrm << " current File count = " << m_filesChecked + 1;
							string error = m_strstrm.str();
							m_strstrm.str("");

							LogError(m_log, error);
							m_runErrors++;
						}
						try
						{
							stmt->execute();
							boost::scoped_ptr<sql::Statement> qstmt(conn->createStatement());
							res = qstmt->executeQuery("SELECT @@identity AS id");
							res->next();
							m_lastRow = res->getUInt("id");
						}
						catch (sql::SQLException x)
						{
							m_strstrm << x.what() << " getting identity for ";
							m_strstrm << inputFile << " in " << m_realIsbn;
							m_strstrm << " current File count = " << m_filesChecked + 1;
							string error = m_strstrm.str();
							m_strstrm.str("");

							LogError(m_log, error);
							m_runErrors++;
						}
						iss.str("");
						m_wordcount = 0;
						if ((inputFile.find(".htm") != string::npos) || (inputFile.find(".xhtml") != string::npos))
						{
							m_wordcount = wordcount(stdMem);
						}
					}
					//
				} // else LONGTEXT
				// Additional sql Inserts are here
				unsigned long m_parent = m_lastRow;
				unsigned long m_item2row, m_nav2row, m_uncLength;
				string m_mimetype, m_thismime;
				basic_string <char>::size_type extPos = 0;
				extPos = inputFile.find_last_of(".");
				if (extPos == string::npos)
				{
					m_mimetype = inputFile;
				} 
				else
				{
					m_thismime = inputFile.substr(extPos, inputFile.length() - extPos);
					boost::algorithm::to_lower(m_thismime);
					if (m_mimeMap.find(m_thismime) == m_mimeMap.end())
					{
						m_strstrm << m_thismime << " is not in the mime-map for ";
						m_strstrm << inputFile << " in " << m_realIsbn;
						m_strstrm << " current File count = " << m_filesChecked + 1;
						string error = m_strstrm.str();
						m_strstrm.str("");
						LogError(m_log, error);
						m_mimetype = inputFile;
					} 
					else
					{
						map<string, string>::iterator mit;
						m_mimetype = m_mimeMap.find(m_thismime)->second;
					}
				}
				// -------------------------------------------------------------------------------
				//  m_uncLength is uncompressed data length --- 
				// -------------------------------------------------------------------------------				
				m_uncLength = entry->get_UncompressedLength();
				int isKey = 0;
				if (!isBlob)
				{
					isKey = regexSearch(inputFile);
					if (isKey == 1)
					{
						didFind = true;
					}
				} 
				else
				{
					isKey = 0;
				}
				// -------------------------------------------------------------------------------
				//  INSERT INTO item2 --- 
				// -------------------------------------------------------------------------------				

				boost::scoped_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("INSERT INTO item2 (epub2_id, eid, mediatype, uncLength, isfirstpage, wordcount) VALUES(?,?,?,?,?,?)"));
				try
				{
					stmt->setUInt(1, m_parent);
					stmt->setString(2, m_realIsbn.c_str());
					stmt->setString(3, m_mimetype.c_str());
					stmt->setUInt(4, m_uncLength);
					stmt->setInt(5, isKey);
					stmt->setUInt(6, m_wordcount);
				}
				catch (sql::SQLException x)
				{
					m_strstrm << x.what() << " while inserting item2 ";
					m_strstrm << inputFile << " in " << m_realIsbn;
					m_strstrm << " current File count = " << m_filesChecked + 1;
					string error = m_strstrm.str();
					m_strstrm.str("");

					LogError(m_log, error);
					m_runErrors++;
				}
				try
				{
					stmt->execute();
					boost::scoped_ptr<sql::Statement> qstmt(conn->createStatement());
					res = qstmt->executeQuery("SELECT @@identity AS id");
					res->next();
					m_item2row = res->getUInt("id");
				}
				catch (sql::SQLException x)
				{
					string error = x.what();
					LogError(m_log, error);
					m_runErrors++;
				}
				// -------------------------------------------------------------------------------
				//  INSERT INTO nav2 --- 
				// -------------------------------------------------------------------------------				
				boost::scoped_ptr<sql::PreparedStatement> stmt3(conn->prepareStatement("INSERT INTO nav2 (parent_id, eid, sequence, item2_id ) VALUES(?,?,?,?)"));
				try
				{
					stmt3->setUInt(1, m_item2row);
					stmt3->setString(2, m_realIsbn.c_str());
					stmt3->setInt(3, i);
					stmt3->setUInt(4, m_item2row);
				}
				catch (sql::SQLException x)
				{
					m_strstrm << x.what() << " while binding nav2 ";
					m_strstrm << inputFile << " in " << m_realIsbn;
					m_strstrm << " current File count = " << m_filesChecked + 1;
					string error = m_strstrm.str();
					m_strstrm.str("");

					LogError(m_log, error);
					m_runErrors++;
				}
				try
				{
					stmt3->execute();
					boost::scoped_ptr<sql::Statement> qstmt(conn->createStatement());
					res = qstmt->executeQuery("SELECT @@identity AS id");
					res->next();
					m_nav2row = res->getUInt("id");
				}
				catch (sql::SQLException x)
				{
					m_strstrm << x.what() << " getting identity for ";
					m_strstrm << inputFile << " in " << m_realIsbn;
					m_strstrm << " current File count = " << m_filesChecked + 1;
					string error = m_strstrm.str();
					m_strstrm.str("");

					LogError(m_log, error);
					m_runErrors++;
				}
				// -------------------------------------------------------------------------------
				//  INSERT INTO spine2 --- 
				// -------------------------------------------------------------------------------
				boost::scoped_ptr<sql::PreparedStatement> spinestmt(conn->prepareStatement("INSERT INTO spine2 (sequence, item2_id) VALUES(?,?)"));
				try
				{
					spinestmt->setInt(1, i);
					spinestmt->setUInt(2, m_item2row);
				}
				catch (sql::SQLException x)
				{
					m_strstrm << x.what() << " while binding spine2 ";
					m_strstrm << inputFile << " in " << m_realIsbn;
					m_strstrm << " current File count = " << m_filesChecked + 1;
					string error = m_strstrm.str();
					m_strstrm.str("");

					LogError(m_log, error);
					m_runErrors++;
				}
				try
				{
					spinestmt->execute();
				}
				catch (sql::SQLException x)
				{
					m_strstrm << x.what() << " while inserting spine2 ";
					m_strstrm << inputFile << " in " << m_realIsbn;
					m_strstrm << " current File count = " << m_filesChecked + 1;
					string error = m_strstrm.str();
					m_strstrm.str("");

					LogError(m_log, error);
					m_runErrors++;
				}

			} // for (i = 0; i <= zcnt - 1; i++)
			if (!didFind)
			{
				++m_notFound;
				m_strstrm << "key page Not Found for ISBN " << m_realIsbn;
				string report = m_strstrm.str();
				m_strstrm.str("");
				LogError(m_log, report);
			}
			zip.CloseZip();
		} // if (pathname.find(".epub") != string::npos)
		m_filesChecked++;
	} // for it(v.begin()), it_end(v.end()

	try
	{
		boost::scoped_ptr<sql::Statement> fcOffstmt(conn->createStatement());
		fcOffstmt->execute("SET FOREIGN_KEY_CHECKS=1");
	}
	catch (sql::SQLException x)
	{
		string error = x.what();
		error += " SET FOREIGN_KEY_CHECKS = 1";
		LogError(m_log, error);
		m_runErrors++;
	}
//	mysql_close(conn);
	delete conn;
	delete res;
	// Fat lady sings
	string iamdone;
	m_strstrm << "Completed run, Files checked is " << m_filesChecked << " and Errors on this run are " << m_runErrors;
	m_strstrm << " and no key File found is " << m_notFound;
	iamdone = m_strstrm.str();
	m_strstrm.str("");
	LogError(m_log, iamdone);
	return 0;
}
// -------------------------------------------------------------------------------
//  Get ISBN from filename --- 
// -------------------------------------------------------------------------------
string NewIdent(string pFile)
{
	size_t fstart = pFile.find("978", 0);
	std::string m_new = pFile.substr(fstart, 13 );
	return m_new;
}
// -------------------------------------------------------------------------------
//  LogError --- 
// -------------------------------------------------------------------------------
void LogError(string lFile, string pMsg)
{
	string m_outMsg;
	std::ostringstream m_strstrm;
	/****** format strings ******/
	const boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
	boost::posix_time::time_facet*const f = new boost::posix_time::time_facet("%a %b %d, %H-%M-%S");
	m_strstrm.imbue(std::locale(m_strstrm.getloc(),f));
	m_strstrm << now;


	m_strstrm << " : " << pMsg << endl;
	m_outMsg = m_strstrm.str();
	m_strstrm.str("");
	fstream myfile;
	//open for appending and append
	myfile.open(lFile.c_str(), ios::out | ios::app);
	if (!myfile.is_open())
	{
		std::cout << "The Log file did not open." << endl << pMsg << endl;
	}
	myfile << m_outMsg.c_str();
	myfile.close();
#ifdef _VERBOSE
	std::cout << pMsg;
#endif
}
// -------------------------------------------------------------------------------
//  if system supports openssl get MD5 --- 
// -------------------------------------------------------------------------------
#ifdef _USE_OPENSSL
std::string getMd5(std::string pFile)
{
	std::ostringstream m_strstrm;
	unsigned char c[MD5_DIGEST_LENGTH];
	int i;
	FILE *inFile = fopen (pFile.c_str(), "rb");
	MD5_CTX mdContext;
	int bytes;
	unsigned char data[1024];

	if (inFile == NULL) {
	std::string m_readerror = "The file did not open for md5 " << pFile << endl;
		LogError(m_log, m_readerror);
		std::cout << m_readerror;
		return 0;
	}

	MD5_Init (&mdContext);
	while ((bytes = fread (data, 1, 1024, inFile)) != 0)
		MD5_Update (&mdContext, data, bytes);
	MD5_Final (c,&mdContext);
	m_strstrm.str("");
	m_strstrm << std::hex << std::setfill('0') << std::uppercase;
	for(i = 0; i < MD5_DIGEST_LENGTH; i++) m_strstrm << std::setw(2) << static_cast<int>(c[i]);
	fclose (inFile);
	return m_strstrm.str();
}
#endif
// -------------------------------------------------------------------------------
//  The boost regex search function --- 
// -------------------------------------------------------------------------------
int regexSearch(const string st) {
	int didFind = 0;
	boost::regex expression(	"(^.*chapter1\\.(xh|ht))|(^.*chapter.*[^1-9]01\\.(xh|ht))|(^.*c[^ornpck]*.*[^1-9]01\\.(xh|ht))|(^.*c1\\.(xh|ht))|(^.*chapter01\\.(xh|ht))|(^.*ch.*[^1-9]01\\.(xh|ht))|(^.*_ch01\\.(xh|ht))", boost::regex::icase);
	std::string::const_iterator start, end;
	start = st.begin();
	end = st.end();   
	boost::match_results<std::string::const_iterator> what;
	boost::match_flag_type flags = boost::match_default;
	while(boost::regex_search(start, end, what, expression, flags))   
	{
		start = what[0].second;      
		// update flags:
		flags |= boost::match_prev_avail;
		flags |= boost::match_not_bob;
		didFind = 1;
	}
	return didFind;
}
// -------------------------------------------------------------------------------
//  Get the word count for the current page --- 
// -------------------------------------------------------------------------------
unsigned int wordcount(const std::string& str)
{
	boost::regex re("<[^>]*>");
	std::string repl = "";
	std::string newout = boost::regex_replace(str, re, repl, boost::match_default | boost::format_all);
	trim(newout); // boost trim to remove white space from string
	// below is the word count section (str replaced with newout for test
	std::stringstream  stream(str);
	std::string        oneWord;
	unsigned int       count = 0;

	std::istream_iterator<std::string> loop = std::istream_iterator<std::string>(stream);
	std::istream_iterator<std::string> end  = std::istream_iterator<std::string>();

	for(;loop != end; ++count, ++loop) { *loop; }
	return count;
}


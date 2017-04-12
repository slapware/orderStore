This is the README file for the ePubStore application. A Linux and windows build in C++

This is for storing upubs files in a MySql for reading with epubFetch application. It produces web pages of the epub contents. Mysql database stores the epub across three tables. The actual contents for each page is in a blob, wordcount and order of pages
are stored along with mime type.

Uses native MySql connector, runs on Linux and windows, but for windows you may have to
not define _USE_OPENSSL if not available. Also uses boost and STL.
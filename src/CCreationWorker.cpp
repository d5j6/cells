/*
 * CCreationWorker.cpp
 *
 *  Created on: 2012-12-12
 *      Author: happykevins@gmail.com
 */

#include "CCreationWorker.h"

#include <stdio.h>
#include <sstream>
#include <set>
#include <assert.h>
#include <tinyxml2.h>

#include "CUtils.h"
#include "CCells.h"
#include "CCreationFactory.h"

#define BYTES_TO_FLUSH (1024 * 512)

namespace cells
{

void* CCreationWorker::working(void* context)
{
	CCreationWorker* worker = (CCreationWorker*) context;

	while (worker->m_working)
	{
		int sem_retv = sem_wait(&(worker->m_sem));
		assert(sem_retv >= 0);

		worker->do_work();
	}

	sem_destroy(&(worker->m_sem));

	return 0;
}

CCreationWorker::CCreationWorker(CCreationFactory* host, size_t no) :
		m_host(host), m_workno(no), m_working(true), m_downloadhandle(this),
		m_downloadbytes(0), m_cachedbytes(0)
{
	assert(host);
	int ret = sem_init(&m_sem, 0, 0);
	assert(ret == 0);
	pthread_create(&m_thread, NULL, CCreationWorker::working, this);
}

CCreationWorker::~CCreationWorker()
{
	m_working = false;
	sem_post(&m_sem);
	pthread_join(m_thread, NULL);
}

void CCreationWorker::post_work(CCell* cell)
{
	assert(cell);
	m_queue.lock();
	m_queue.push(cell);
	m_queue.unlock();
	sem_post(&m_sem);
}

void CCreationWorker::do_work()
{
	// get a task
	m_queue.lock();
	if (m_queue.empty())
	{
		m_queue.unlock();
		return;
	}

	CCell* cell = m_queue.pop_front();
	m_queue.unlock();

	// make local url
	std::stringstream ss;
	ss << m_host->m_host->regulation().local_url << cell->m_name;
	std::string localurl = ss.str();
	ss << m_host->m_host->regulation().tempfile_suffix;
	std::string localtmpurl = ss.str();
	ss << m_host->m_host->regulation().temphash_suffix;
	std::string localhashurl = ss.str();
	CLogD("local path=%s\n", localurl.c_str());

	// open local file
	FILE* fp = fopen(localurl.c_str(), "rb");
	if (!fp)
	{
		//
		// can not open local file
		//

		if ( m_host->m_host->regulation().only_local_mode )
		{
			cell->m_errorno = e_loaderr_openfile_failed;
			work_finished(cell);
			return;
		}
	}
	else
	{
		//
		// check local file
		//

		// only local mode hack, ignore file verify!
		if ( m_host->m_host->regulation().only_local_mode )
		{
			fclose(fp);

			if ( !work_patchup_cell(cell, localurl.c_str()) )
			{
				cell->m_errorno = e_loaderr_patchup_failed;
			}

			work_finished(cell);
			return;
		}

		// verify local file
		if ( work_verify_local(cell, fp) )
		{
			fclose(fp);

			// patchup local file
			if ( !work_patchup_cell(cell, localurl.c_str()) )
			{
				cell->m_errorno = e_loaderr_patchup_failed;
			}

			work_finished(cell);
			return;
		}
		else
		{
			// 虽然本地验证失败，但这里不用设置错误码，后面还要下载再验证
			//cell->m_errorno == e_loaderr_verify_failed;
			CLogD("local file exist, but verfiy failed, try downloading. %s \n", localurl.c_str());
		}

		// close file
		fclose(fp);
	}

	// setup download environment
	std::string downloadurl = localtmpurl;
	bool need_decompress = false;
	if ( cell->m_ziptype != e_zip_none )
	{
		need_decompress = true;
	}
	/** use cdf 'zip' mark
	// check need tmp file
	if ( m_host->m_host->regulation().zip_type != 0 && 
		( cell->m_celltype == e_state_file_common || m_host->m_host->regulation().zip_cdf ) )
	{
		need_decompress = true;
	}
	*/

	// download
	eloaderror_t download_errno = work_download_remote(
		cell,
		need_decompress,
		downloadurl.c_str(), 
		localhashurl.c_str());

	// download & patchup cell
	if (  download_errno == e_loaderr_ok )
	{
		//
		// download success!
		//
		
		// decompress
		if ( need_decompress )
		{
			if ( !work_decompress(downloadurl.c_str(), localurl.c_str()) )
			{
				CLogE("file decompress failed: name=%s;\n", cell->m_name.c_str());
				cell->m_errorno = e_loaderr_decompress_failed;
			}
		}
		else
		{
			// change name from download temp to local
			if ( CUtils::access(localurl.c_str(), 0) )
			{
				bool rm_ret = CUtils::remove(localurl.c_str());
				assert(rm_ret);
			}
			bool rename_ret = CUtils::rename(downloadurl.c_str(), localurl.c_str());
			assert(rename_ret);
		}

		// verify downloaded file
		if ( cell->m_errorno == e_loaderr_ok && !cell->m_hash.empty() )
		{
			fp = fopen(localurl.c_str(), "rb");
			if (fp)
			{
				if ( !work_verify_local(cell, fp) )
				{
					cell->m_errorno = e_loaderr_verify_failed;
				}
				fclose(fp);
			}
			else
			{
				cell->m_errorno = e_loaderr_openfile_failed;
			}
		}

		// patchup
		if ( cell->m_errorno == e_loaderr_ok )
		{
			if ( !work_patchup_cell(cell, localurl.c_str()) )
			{
				cell->m_errorno = e_loaderr_patchup_failed;
			}
		}
	}
	else
	{
		//
		// download failed
		//
		cell->m_errorno = download_errno;
	}

	work_finished(cell);	
}

size_t CCreationWorker::workload()
{
	CMutexScopeLock(m_queue.mutex());
	return m_queue.size();
}

void CCreationWorker::work_finished(CCell* cell)
{
	// notify factory work done!
	m_host->notify_work_finished(cell);
}

size_t CCreationWorker::get_downloadbytes()
{
	return m_downloadbytes;
}

size_t CCreationWorker::calc_maxspeed()
{
	return m_host->suggest_maxspeed();
}

bool CCreationWorker::work_verify_local(CCell* cell, FILE* fp)
{
	if (cell->m_hash.empty())
	{
		CLogI("hash code not specified: name=%s;\n", cell->m_name.c_str());
		return false;
	}

	// 验证本地文件hash
	assert(fp);

	std::string md5str = CUtils::filehash_md5str(fp, m_databuf, sizeof(m_databuf));

	if ( md5str != cell->m_hash )
	{
		CLogD("hash verify failed: name=%s; cdf_hash=%s, file_hash=%s\n", cell->m_name.c_str(), cell->m_hash.c_str(), md5str.c_str());
		return false;
	}
	else
	{
		CLogD("hash verify success: name=%s; hash=%s\n", cell->m_name.c_str(), cell->m_hash.c_str());
	}

	return true;
}

eloaderror_t CCreationWorker::work_download_remote(CCell* cell, bool zip_mark, const char* localurl, const char* localhashurl)
{
	//
	// 检查hash文件，是否需要断点续传
	//
	bool bp_resume = false;
	size_t bp_range_begin = 0;
	if ( CUtils::access(localhashurl, 0) )
	{
		FILE* hash_fp = fopen(localhashurl, "r");
		if ( hash_fp )
		{
			char tmp_buf[33];
			if ( ::fgets(tmp_buf, 33, hash_fp) )
			{
				std::string bp_hash = CUtils::str_trim(std::string(tmp_buf));

				if ( !cell->m_hash.empty() )
				{
					if ( (zip_mark && cell->m_zhash == bp_hash)
						|| (!zip_mark && cell->m_hash == bp_hash ) )
					{
						bp_resume = true;
					}
				}
			}
		}

		fclose(hash_fp);
		CUtils::remove(localhashurl);
	}

	//
	// create & check local file
	//
	FILE* fp = bp_resume ? 
		fopen(localurl, "ab+"):
		fopen(localurl, "wb+");
	if (!fp)
	{
		bp_resume = false;

		// build path directory, try again!
		CUtils::builddir(localurl);
		fp = fopen(localurl, "wb+");
		if ( !fp )
		{
			CLogE("download error: can't create local file: name=%s\n", cell->m_name.c_str());
			return e_loaderr_openfile_failed;
		}	
	}
	// set bp range
	if ( bp_resume )
	{
		fseek(fp, 0, SEEK_END);
		bp_range_begin = (size_t)ftell(fp);
	}

	//
	// make remote url
	//
	const std::vector<std::string>& urls = m_host->m_host->regulation().remote_urls;
	int urlidx = cell->m_download_times % urls.size();
	std::stringstream ss;
	ss << urls[urlidx] << cell->m_name.c_str() << m_host->m_host->regulation().remote_zipfile_suffix;

	//
	// write bp resume file
	//
	if ( !cell->m_hash.empty() )
	{
		FILE* hash_fp = fopen(localhashurl, "w+");
		if ( hash_fp )
		{
			if ( zip_mark )
				fprintf(hash_fp, "%s", cell->m_zhash.c_str());
			else
				fprintf(hash_fp, "%s", cell->m_hash.c_str());
			fclose(hash_fp);
		}
	}

	//
	// perform download
	//
	CDownloader::edownloaderr_t result = m_downloadhandle.download(ss.str().c_str(), fp, bp_resume, bp_range_begin);

	//
	// close out
	//
	fclose(fp);

	// increase the download times counter
	cell->m_download_times++;

	// no download error
	if ( result == CDownloader::e_downloaderr_ok )
	{
		CUtils::remove(localhashurl);
		CLogD("download cell success: name=%s\n", cell->m_name.c_str());
		return e_loaderr_ok;
	}

	// errors can't bp resume
	if ( result == CDownloader::e_downloaderr_other_nobp )
	{
		CUtils::remove(localhashurl);
	}

	CLogI("download cell failed: name=%s\n", cell->m_name.c_str());

	return e_loaderr_download_failed;
}

bool CCreationWorker::work_decompress(const char* tmplocalurl, const char* localurl)
{
	bool ret = CUtils::decompress(tmplocalurl, localurl) == 0;
	bool rm_ret = CUtils::remove(tmplocalurl);
	assert(rm_ret);
	return ret;
}

/**
// cocos2d xml parser
class CDFParser: public CCSAXDelegator
{
public:
	CCDF* parse(CCell* cell, const char* localpath)
	{
		CCSAXParser parser;
		if ( !parser.init("UTF-8") )
		{
			CCLog("[Cells] CCSAXParser.init failed! when load file: %s", localpath);
			return NULL;
		}
		parser.setDelegator(this);

		m_cdf = new CCDF(cell);

		if ( parser.parse(localpath) )
			return m_cdf;

		CC_SAFE_DELETE(m_cdf);
		return NULL;
	}

	virtual void startElement(void *ctx, const char *name, const char **atts)
	{	
		assert(m_cdf);

		if ( strcmp(name, "cell") == 0 && *atts )
		{
			props_t attrmap;
			for ( size_t i = 0; atts[i] != NULL && atts[i+1] != NULL; i+=2 )
			{
				attrmap.insert(std::make_pair<std::string, std::string>(atts[i], atts[i+1]));
			}

			// parse name
			const char* cell_name = NULL;
			if ( attrmap.find(CDF_CELL_NAME) != attrmap.end() )
			{
				cell_name = attrmap[CDF_CELL_NAME].c_str();
			}

			if (!cell_name )
			{
				CLogI("[Cells] null cell name in cdf: %s. \n", m_cdf->m_hostcell->m_name.c_str());
				return;
			}

			std::string cell_name_tmp = CUtils::str_trim(cell_name);
			if ( cell_name_tmp.empty() )
			{
				CLogI("[Cells] empty cell name in cdf: %s. \n", m_cdf->m_hostcell->m_name.c_str());
				return;
			}

			CUtils::str_replace_ch(cell_name_tmp, '\\', '/');
			if ( cell_name_tmp.find_first_of('/') != 0 )	
				cell_name_tmp = "/" + cell_name_tmp;

			// parse hash & zip type
			const char* cell_hash = NULL;
			const char* cell_zhash = NULL;
			eziptype_t cell_ziptype = e_zip_none;

			if ( attrmap.find(CDF_CELL_HASH) != attrmap.end() )
			{
				cell_hash = attrmap[CDF_CELL_HASH].c_str();
			}
			if ( attrmap.find(CDF_CELL_ZHASH) != attrmap.end() )
			{
				cell_zhash = attrmap[CDF_CELL_ZHASH].c_str();
			}

			if ( attrmap.find(CDF_CELL_ZIP) != attrmap.end() )
			{
				int cell_ziptype_tmp = CUtils::atoi(attrmap[CDF_CELL_ZIP].c_str());
				if ( cell_ziptype_tmp != 0 )
				{
					cell_ziptype = e_zip_zlib;
				}
			}

			// parse file type
			estatetype_t cell_type = e_state_file_common;

			if ( attrmap.find(CDF_CELL_CDF) != attrmap.end() )
			{
				bool is_cdf = CUtils::atoi(attrmap[CDF_CELL_CDF].c_str()) == 1;
				if ( is_cdf )
				{
					cell_type = e_state_file_cdf;
				}
			}

			// empty hash?
			if (!cell_hash)
			{
				CLogI("[Cells] hash code not specified for %s. \n", cell_name);
				cell_hash = "";
			}

			CCell* cell = new CCell(cell_name_tmp, cell_hash, cell_type);

			if (cell_zhash) 
				cell->m_zhash = cell_zhash;

			cell->m_ziptype = cell_ziptype;

			cell->m_props = attrmap;

			// add cell to cdf
			m_cdf->m_subcells.push_back(cell);
		}
		else if ( strcmp(name, "cells") == 0 && *atts )
		{
			for ( size_t i = 0; atts[i] != NULL && atts[i+1] != NULL; i+=2 )
			{
				m_cdf->m_props.insert(
					std::make_pair<std::string, std::string>(atts[i], atts[i+1]));
			}
		}
	}

	virtual void endElement(void *ctx, const char *name) {}
	virtual void textHandler(void *ctx, const char *s, int len) {}

	CDFParser() 
		: m_cdf(NULL) 
	{
	}

private:
	CCDF* m_cdf;

};//CellParser
**/

bool CCreationWorker::work_patchup_cell(CCell* cell, const char* localurl)
{
	bool cdf_result = false;

	// setup cdf
	using namespace tinyxml2;
	//TiXmlDocument doc;
	tinyxml2::XMLDocument doc;
	if ( cell->m_celltype == e_state_file_cdf && XML_SUCCESS == doc.LoadFile(localurl) )
	{
		std::set<const char*> name_set;
		CCDF* ret_cdf = new CCDF(cell);
		for (XMLElement* cells_section = doc.FirstChildElement();
			cells_section; cells_section =
			cells_section->NextSiblingElement())
		{
			for (const XMLAttribute* cells_attr =
				cells_section->FirstAttribute(); cells_attr; cells_attr =
				cells_attr->Next())
			{
				//CLogD("%s=%s\n", cells_attr->Name(), cells_attr->Value());
				ret_cdf->m_props.insert(
					std::make_pair(cells_attr->Name(),
					cells_attr->Value()));
			}

			for (const XMLElement* cell_section =
				cells_section->FirstChildElement(); cell_section;
				cell_section = cell_section->NextSiblingElement())
			{
				const char* cell_name = cell_section->Attribute(CDF_CELL_NAME);

				if (!cell_name )
				{
					CLogI("null cell name in cdf: %s. \n", cell->m_name.c_str());
					continue;
				}
				std::string cell_name_tmp = CUtils::str_trim(cell_name);
				if ( cell_name_tmp.empty() )
				{
					CLogI("empty cell name in cdf: %s. \n", cell->m_name.c_str());
					continue;
				}
				CUtils::str_replace_ch(cell_name_tmp, '\\', '/');
				if ( cell_name_tmp.find_first_of('/') != 0 )	cell_name_tmp = "/" + cell_name_tmp;

				const char* cell_hash = cell_section->Attribute(CDF_CELL_HASH);
				const char* cell_zhash = cell_section->Attribute(CDF_CELL_ZHASH);
				int cell_ziptype_tmp = CUtils::atoi(cell_section->Attribute(CDF_CELL_ZIP));
				eziptype_t cell_ziptype = e_zip_none;
				if ( cell_ziptype_tmp != 0 )
				{
					cell_ziptype = e_zip_zlib;
				}

				estatetype_t cell_type = e_state_file_common;
				bool is_cdf = CUtils::atoi(cell_section->Attribute(CDF_CELL_CDF)) == 1;
				if (is_cdf)
					cell_type = e_state_file_cdf;
				if (!cell_hash)
				{
					CLogI("hash code not specified for %s. \n", cell_name);
					cell_hash = "";
				}

				CCell* cell = new CCell(cell_name_tmp, cell_hash, cell_type);
				if (cell_zhash) cell->m_zhash = cell_zhash;
				cell->m_ziptype = cell_ziptype;

				for (const XMLAttribute* cell_attr =
					cell_section->FirstAttribute(); cell_attr; cell_attr =
					cell_attr->Next())
				{
					//CLogD("%s=%s\n", cell_attr->Name(), cell_attr->Value());
					cell->m_props.insert(
						std::make_pair(cell_attr->Name(),
						cell_attr->Value()));
				}

				name_set.insert(cell_name);
				ret_cdf->m_subcells.push_back(cell);
			}
		}
		cdf_result = true;
		cell->m_cdf = ret_cdf;
	}

	/**
	// cocos2dx implement
	CDFParser parser;
	if ( cell->m_celltype == e_state_file_cdf
		&& (cell->m_cdf = parser.parse(cell, localurl)) )
	{
		cdf_result = true;
	}
	**/

	// cdf file
	if ( !cdf_result && cell->m_celltype == e_state_file_cdf )
	{
		CLogE("cdf setup failed!: name=%s\n", cell->m_name.c_str() );
		return false;
	}
	else if ( cell->m_celltype == e_state_file_cdf )
	{
		CLogI("cdf setup success: name=%s, child=%d\n", cell->m_name.c_str(), (int)cell->m_cdf->m_subcells.size());
		return true;
	}

	// common file

	return true;
}

bool CCreationWorker::on_download_bytes(size_t bytes)
{
	m_downloadbytes += bytes;
	m_cachedbytes += bytes;

	// check should flush
	if ( m_cachedbytes >=  BYTES_TO_FLUSH)
	{
		m_cachedbytes = 0;
		return true;
	}
	return false;
}

CGhostWorker::CGhostWorker(CCreationFactory* host, size_t no)
	: CCreationWorker(host, no)
{

}
CGhostWorker::~CGhostWorker()
{

}

size_t CGhostWorker::calc_maxspeed()
{
	return m_host->m_host->regulation().max_ghost_download_speed;
}


} /* namespace cells */

/*
* cells.h
*
*  Created on: 2012-12-18
*      Author: happykevins@gmail.com
*/

#ifndef CELLS_H_
#define CELLS_H_

#include <vector>
#include <map>
#include <string>

#define CELLS_DEFAULT_WORKERNUM		2
#define CELLS_DEFAULT_ZIPTMP_SUFFIX	".tmp"

namespace cells
{

//
// �ڽ�������
//
extern const char* CDF_VERSION;			//= "version"	string
extern const char* CDF_LOADALL;			//= "load"		boolean:		0 | 1
extern const char* CDF_CELL_TYPE;		//= "type"		celltype_t: 	0 | 1
extern const char* CDF_CELL_NAME;		//= "name"		string
extern const char* CDF_CELL_HASH;		//= "hash"		string
extern const char* CDF_CELL_LOAD;		//= "load"		boolean:		0 | 1

// �����б�
typedef std::map<std::string, std::string> properties_t; 

// ѹ������
enum eziptype_t
{
	e_nozip = 0,
	e_zlib,
};

// �ļ�����
enum ecelltype_t
{
	e_celltype_common = 0, 
	e_celltype_cdf = 1
};

// ���ش�������
enum eloaderror_t
{
	loaderr_ok = 0, 
	loaderr_openfile_failed, 
	loaderr_download_failed, 
	loaderr_decompress_failed, 
	loaderr_verify_failed, 
	loaderr_patchup_failed
};

//
// cellsϵͳ������
//
struct CRegulation
{
	std::vector<std::string> remote_urls;
	std::string local_url;
	size_t worker_thread_num;
	bool auto_dispatch;
	bool only_local_mode;				// �Ƿ�������ģʽ�������ļ���ƥ��Ҳ������download����
	bool enable_ghost_mode;				// �Ƿ���ghostģʽ
	bool enable_free_download;			// �Ƿ�����������ģʽ��(Ĭ�Ϲر�)��������ģʽ������������cdfû�����������ļ�
	eziptype_t zip_type;				// ѹ�����ͣ�0-δѹ����1-zlib
	bool zip_cdf;						// cdf�ļ��Ƿ�Ϊѹ����ʽ
	const char* zip_tmp_suffix;			// ��ѹ����ʱ�ļ���׺

	// default value
	CRegulation() : worker_thread_num(CELLS_DEFAULT_WORKERNUM), auto_dispatch(false), only_local_mode(false), 
		enable_ghost_mode(false), enable_free_download(false), zip_type(e_nozip), zip_cdf(false),
		zip_tmp_suffix(CELLS_DEFAULT_ZIPTMP_SUFFIX)
	{}
};

//
// CFunctorBase-��װ�����հ�
//
class CFunctorBase
{
public:
	virtual ~CFunctorBase(){}
	virtual void operator() (const std::string& name, ecelltype_t type, eloaderror_t error_no, const properties_t& props) = 0;
};

class CFunctorG : public CFunctorBase
{
public:
	typedef void (*cb_func_g_t)(const std::string& name, ecelltype_t type, eloaderror_t error_no, const properties_t& props);
	CFunctorG(cb_func_g_t cb_func) : m_cb_func(cb_func) {}
	CFunctorG(const CFunctorG& other) : m_cb_func(other.m_cb_func) {}
	CFunctorG() : m_cb_func(NULL) {}
	virtual ~CFunctorG(){ m_cb_func = NULL; }
	virtual void operator() (const std::string& name, ecelltype_t type, eloaderror_t error_no, const properties_t& props)
	{
		m_cb_func(name, type, error_no, props);
	}

protected:
	cb_func_g_t m_cb_func;
};

template<typename T>
class CFunctorM : public CFunctorBase
{
public:
	typedef void (T::*mfunc_t)(const std::string& name, ecelltype_t type, eloaderror_t error_no, const properties_t& props);
	CFunctorM(T* _t, mfunc_t _f) : m_target(_t), m_func(_f) {}
	CFunctorM(const CFunctorM<T>& other) : m_target(other.m_target), m_func(other.m_func) {}
	void operator=(const CFunctorM<T>& other)
	{
		m_target = other.m_target;
		m_func = other.m_func;
	}

	void operator() (const std::string& name, ecelltype_t type, eloaderror_t error_no, const properties_t& props)
	{
		(m_target->*m_func)(name, type, error_no, props);
	}

protected:
	T* m_target;
	mfunc_t m_func;
};

template<typename F>
CFunctorG* make_functor_g(F& _f)
{
	return new CFunctorG(_f);
}
template<typename T>
CFunctorM<T>* make_functor_m(T* _t, typename CFunctorM<T>::mfunc_t _f)
{
	return new CFunctorM<T>(_t, _f);
}

}/* namespace cells */
#endif /* CELLS_H_ */
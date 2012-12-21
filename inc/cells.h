/*
* cells.h
*
*  Created on: 2012-12-18
*      Author: happykevins@gmail.com
*/

#ifndef CELLS_H_
#define CELLS_H_

#include <vector>
#include <list>
#include <map>
#include <string>

#define CELLS_DEFAULT_WORKERNUM			4
#define CELLS_WORKER_MAXWORKLOAD		2
#define CELLS_DOWNLAOD_SPEED_NOLIMIT	(1024 * 1024 * 100) // 100MB
#define CELLS_GHOST_DOWNLAOD_SPEED		(1024 * 32)			// 32KB
#define CELLS_DEFAULT_ZIPTMP_SUFFIX		".tmp"

namespace cells
{

//
// �ڽ�������
//
extern const char* CDF_VERSION;			//= "version"	string
extern const char* CDF_LOADALL;			//= "loadall"	boolean:		0 | 1
extern const char* CDF_CELL_CDF;		//= "cdf"		boolean: 		0 | 1 : is 'e_celltype_cdf' type
extern const char* CDF_CELL_NAME;		//= "name"		string
extern const char* CDF_CELL_HASH;		//= "hash"		string
extern const char* CDF_CELL_LOAD;		//= "load"		boolean:		0 | 1

// ���Ա�
typedef std::map<std::string, std::string> props_t; 
// ���Ա��б�
typedef std::map<std::string, props_t*> props_list_t;

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

// ���ȼ�
enum epriority_t {
	e_priority_ghost		= -1,
	e_priority_default 		= 0,
	e_priority_exclusive 	= (unsigned short)-1, // ���65535
};

// CDF�ļ����ط�ʽ
//  *ע�⣺Ŀǰ��cascade���ط�ʽ�������ڲ�ʵ����һ��cdf����״̬������ֹǱ�ڵ����޵ݹ飬
//		�������cascade·���������Ѿ�������������cdf������ֹ�ݹ飬����·��Ҳ���ᱻ���ء�
enum ecdf_loadtype_t
{
	e_cdf_loadtype_config = 0,		// ������cdf�������������������м��ز���
	e_cdf_loadtype_index,			// ֻ������cdf���������������ļ�
	e_cdf_loadtype_index_cascade,	// ����������cdf������������common�ļ�
	e_cdf_loadtype_load,			// �������������ش�cdf�������ļ�
	e_cdf_loadtype_load_cascade,	// ������������������������cdf�������ļ�
};

// ���ش�������
enum eloaderror_t
{
	e_loaderr_ok = 0, 
	e_loaderr_openfile_failed, 
	e_loaderr_download_failed, 
	e_loaderr_decompress_failed, 
	e_loaderr_verify_failed, 
	e_loaderr_patchup_failed
};

//
// cellsϵͳ������
//
struct CRegulation
{
	std::vector<std::string> remote_urls;
	std::string local_url;
	size_t worker_thread_num;
	size_t max_download_speed;
	bool auto_dispatch;
	bool only_local_mode;				// �Ƿ�������ģʽ�������ļ���ƥ��Ҳ������download����
	bool enable_ghost_mode;				// �Ƿ���ghostģʽ
	size_t max_ghost_download_speed;	// ghost�������ٶ�
	bool enable_free_download;			// �Ƿ�����������ģʽ��(Ĭ�Ϲر�)��������ģʽ������������cdfû�����������ļ�
	eziptype_t zip_type;				// ѹ�����ͣ�0-δѹ����1-zlib
	bool zip_cdf;						// cdf�ļ��Ƿ�Ϊѹ����ʽ
	std::string tempfile_suffix;		// ��ѹ����ʱ�ļ���׺

	// default value
	CRegulation() : worker_thread_num(CELLS_DEFAULT_WORKERNUM), max_download_speed(CELLS_DOWNLAOD_SPEED_NOLIMIT),
		auto_dispatch(false), only_local_mode(false), 
		enable_ghost_mode(false), max_ghost_download_speed(CELLS_GHOST_DOWNLAOD_SPEED),
		enable_free_download(false), zip_type(e_nozip), zip_cdf(false),
		tempfile_suffix(CELLS_DEFAULT_ZIPTMP_SUFFIX)
	{}
};

//
// CFunctorBase-��װ�����հ�
//
class CFunctorBase
{
public:
	virtual ~CFunctorBase(){}
	virtual void operator() (const std::string& name, ecelltype_t type, eloaderror_t error_no, const props_t* props, const props_list_t* sub_props, void* context) = 0;
};

class CFunctorG : public CFunctorBase
{
public:
	typedef void (*cb_func_g_t)(const std::string& name, ecelltype_t type, eloaderror_t error_no, const props_t* props, const props_list_t* sub_props, void* context);
	CFunctorG(cb_func_g_t cb_func) : m_cb_func(cb_func) {}
	CFunctorG(const CFunctorG& other) : m_cb_func(other.m_cb_func) {}
	CFunctorG() : m_cb_func(NULL) {}
	virtual ~CFunctorG(){ m_cb_func = NULL; }
	virtual void operator() (const std::string& name, ecelltype_t type, eloaderror_t error_no, const props_t* props, const props_list_t* sub_props, void* context)
	{
		m_cb_func(name, type, error_no, props, sub_props, context);
	}

protected:
	cb_func_g_t m_cb_func;
};

template<typename T>
class CFunctorM : public CFunctorBase
{
public:
	typedef void (T::*mfunc_t)(const std::string& name, ecelltype_t type, eloaderror_t error_no, const props_t* props, const props_list_t* sub_props, void* context);
	CFunctorM(T* _t, mfunc_t _f) : m_target(_t), m_func(_f) {}
	CFunctorM(const CFunctorM<T>& other) : m_target(other.m_target), m_func(other.m_func) {}
	void operator=(const CFunctorM<T>& other)
	{
		m_target = other.m_target;
		m_func = other.m_func;
	}

	void operator() (const std::string& name, ecelltype_t type, eloaderror_t error_no, const props_t* props, const props_list_t* sub_props, void* context)
	{
		(m_target->*m_func)(name, type, error_no, props, sub_props, context);
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
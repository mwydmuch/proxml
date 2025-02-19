#ifndef _TRON_H
#define _TRON_H

class function
{
public:
	virtual double fun(double *w) = 0 ;
	virtual void grad(double *w, double *g) = 0 ;
	virtual void Hv(double *s, double *Hs) = 0 ;

	virtual long long get_nr_variable(void) = 0 ;
	virtual ~function(void){}
};

class functionM
{
public:
	virtual double fun(double *w) = 0 ;
	virtual void grad(double *w, double *g) = 0 ;

	virtual long long get_nr_variable(void) = 0 ;
	virtual ~functionM(void){}
};

class TRON
{
public:
	TRON(const function *fun_obj, double eps = 0.1, long long max_iter = 1000);
	~TRON();

	void tron(double *w);
	void set_print_string(void (*i_print) (const char *buf));

private:
	long long trcg(double delta, double *g, double *s, double *r);
	double norm_inf(long long n, double *x);

	double eps;
	long long max_iter;
	function *fun_obj;
	void info(const char *fmt,...);
	void (*tron_print_string)(const char *buf);
};
#endif

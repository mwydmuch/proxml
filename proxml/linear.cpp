#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <locale.h>
#include "linear.h"
#include "tron.h"
#include <omp.h>
#include <vector>
#include <limits>

#ifdef __cplusplus
extern "C" {
#endif

extern double dnrm2_(long long *, double *, long long *);
extern double ddot_(long long *, double *, long long *, double *, long long *);
extern long long daxpy_(long long *, double *, double *, long long *, double *, long long *);
extern long long dscal_(long long *, double *, double *, long long *);

#ifdef __cplusplus
}
#endif

typedef signed char schar;
template <class T> static inline void swap(T& x, T& y) { T t=x; x=y; y=t; }
#ifndef min
template <class T> static inline T min(T x,T y) { return (x<y)?x:y; }
#endif
#ifndef max
template <class T> static inline T max(T x,T y) { return (x>y)?x:y; }
#endif
template <class S, class T> static inline void clone(T*& dst, S* src, long long n)
{
	dst = new T[n];
	memcpy((void *)dst,(void *)src,sizeof(T)*n);
}
#define Malloc(type,n) (type *)malloc((n)*sizeof(type))
#define INF HUGE_VAL

static void print_string_stdout(const char *s)
{
	fputs(s,stdout);
	fflush(stdout);
}

static void (*liblinear_print_string) (const char *) = &print_string_stdout;

#if 1
static void info(const char *fmt,...)
{
	char buf[BUFSIZ];
	va_list ap;
	va_start(ap,fmt);
	vsprintf(buf,fmt,ap);
	va_end(ap);
	(*liblinear_print_string)(buf);
}
#else
static void info(const char *fmt,...) {}
#endif

class l2r_lr_fun: public function
{
public:
	l2r_lr_fun(const subproblem *prob, double *C);
	~l2r_lr_fun();

	double fun(double *w);
	void grad(double *w, double *g);
	void Hv(double *s, double *Hs);

	long long get_nr_variable(void);

private:
	void Xv(double *v, double *Xv);
	void XTv(double *v, double *XTv);

	double *C;
	double *z;
	double *D;
	const subproblem *prob;
};

l2r_lr_fun::l2r_lr_fun(const subproblem *prob, double *C)
{
	long long l=prob->l;

	this->prob = prob;

	z = new double[l];
	D = new double[l];
	this->C = C;
}

l2r_lr_fun::~l2r_lr_fun()
{
	delete[] z;
	delete[] D;
}


double l2r_lr_fun::fun(double *w)
{
	long long i;
	double f=0;
	double *y=prob->y;
	long long l=prob->l;
	long long w_size=get_nr_variable();

	Xv(w, z);

	for(i=0;i<w_size;i++)
		f += w[i]*w[i];
	f /= 2.0;
	for(i=0;i<l;i++)
	{
		double yz = y[i]*z[i];
		if (yz >= 0)
			f += C[i]*log(1 + exp(-yz));
		else
			f += C[i]*(-yz+log(1 + exp(yz)));
	}

	return(f);
}

void l2r_lr_fun::grad(double *w, double *g)
{
	long long i;
	double *y=prob->y;
	long long l=prob->l;
	long long w_size=get_nr_variable();

	for(i=0;i<l;i++)
	{
		z[i] = 1/(1 + exp(-y[i]*z[i]));
		D[i] = z[i]*(1-z[i]);
		z[i] = C[i]*(z[i]-1)*y[i];
	}
	XTv(z, g);

	for(i=0;i<w_size;i++)
		g[i] = w[i] + g[i];
}

long long l2r_lr_fun::get_nr_variable(void)
{
	return prob->n;
}

void l2r_lr_fun::Hv(double *s, double *Hs)
{
	long long i;
	long long l=prob->l;
	long long w_size=get_nr_variable();
	double *wa = new double[l];

	Xv(s, wa);
	for(i=0;i<l;i++)
		wa[i] = C[i]*D[i]*wa[i];

	XTv(wa, Hs);
	for(i=0;i<w_size;i++)
		Hs[i] = s[i] + Hs[i];
	delete[] wa;
}

void l2r_lr_fun::Xv(double *v, double *Xv)
{
	long long i;
	long long l=prob->l;
	feature_node **x=prob->x;

	for(i=0;i<l;i++)
	{
		feature_node *s=x[i];
		Xv[i]=0;
		while(s->index!=-1)
		{
			Xv[i]+=v[s->index-1]*s->value;
			s++;
		}
	}
}

void l2r_lr_fun::XTv(double *v, double *XTv)
{
	long long i;
	long long l=prob->l;
	long long w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		XTv[i]=0;
	for(i=0;i<l;i++)
	{
		feature_node *s=x[i];
		while(s->index!=-1)
		{
			XTv[s->index-1]+=v[i]*s->value;
			s++;
		}
	}
}

class l1r_l2_svc_prox_fun : public functionM
{
public:
	l1r_l2_svc_prox_fun(const subproblem *prob, double *C);
	~l1r_l2_svc_prox_fun();

	double fun(double *w);
	void grad(double *w, double *g);

	long long get_nr_variable(void);

protected:
	void Xv(double *v, double *Xv);
	void subXTv(double *v, double *XTv);

	double *C;
	double *z;
	double *D;
	long long *I;
	long long sizeI;
	const subproblem *prob;
};

l1r_l2_svc_prox_fun::l1r_l2_svc_prox_fun(const subproblem *prob, double *C)
{
	long long l=prob->l;

	this->prob = prob;
	z = new double[l];
	I = new long long[l];
	this->C = C;
}

l1r_l2_svc_prox_fun::~l1r_l2_svc_prox_fun()
{
	delete[] z;
	delete[] I;
}

double l1r_l2_svc_prox_fun::fun(double *w)
{

	double f=0;
	double *y=prob->y;
	long long l=prob->l;

	Xv(w, z);

	for(long long i=0;i<l;i++)
	{
		z[i] = y[i]*z[i];
		double d = 1-z[i];
		if (d > 0)
			f += C[i]*d*d;
	}

	return(f);
}



void l1r_l2_svc_prox_fun::grad(double *w, double *g)
{

	double *y=prob->y;
	long long l=prob->l;

	sizeI = 0;

	for (long long i=0;i<l;i++)
		if (z[i] < 1)
		{
			z[sizeI] = C[i]*y[i]*(z[i]-1);
			I[sizeI] = i;
			sizeI++;
		}
	subXTv(z, g);

	for (int i = 0; i < prob->n; i++)
		g[i] = 2 * g[i];

}

long long l1r_l2_svc_prox_fun::get_nr_variable(void)
{
	return prob->n;
}

void l1r_l2_svc_prox_fun::Xv(double *v, double *Xv)
{
	long long i;
	long long l=prob->l;
	feature_node **x=prob->x;

	for(i=0;i<l;i++)
	{
		feature_node *s=x[i];
		Xv[i]=0;
		while(s->index!=-1)
		{
			Xv[i]+=v[s->index-1]*s->value;
			s++;
		}
	}
}

void l1r_l2_svc_prox_fun::subXTv(double *v, double *XTv)
{
	long long i;
	long long w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		XTv[i]=0;
	for(i=0;i<sizeI;i++)
	{
		feature_node *s=x[I[i]];
		while(s->index!=-1)
		{
			XTv[s->index-1]+=v[i]*s->value;
			s++;
		}
	}
}


class l2r_l2_svc_fun: public function
{
public:
	l2r_l2_svc_fun(const subproblem *prob, double *C);
	~l2r_l2_svc_fun();

	double fun(double *w);
	void grad(double *w, double *g);
	void Hv(double *s, double *Hs);

	long long get_nr_variable(void);

protected:
	void Xv(double *v, double *Xv);
	void subXv(double *v, double *Xv);
	void subXTv(double *v, double *XTv);

	double *C;
	double *z;
	double *D;
	long long *I;
	long long sizeI;
	const subproblem *prob;
};

l2r_l2_svc_fun::l2r_l2_svc_fun(const subproblem *prob, double *C)
{
	long long l=prob->l;

	this->prob = prob;

	z = new double[l];
	D = new double[l];
	I = new long long[l];
	this->C = C;
}

l2r_l2_svc_fun::~l2r_l2_svc_fun()
{
	delete[] z;
	delete[] D;
	delete[] I;
}

double l2r_l2_svc_fun::fun(double *w)
{
	long long i;
	double f=0;
	double *y=prob->y;
	long long l=prob->l;
	long long w_size=get_nr_variable();

	Xv(w, z);

	for(i=0;i<w_size;i++)
		f += w[i]*w[i];
	f /= 2.0;

	for(i=0;i<l;i++)
	{
		z[i] = y[i]*z[i];
		double d = 1-z[i];
		if (d > 0)
			f += C[i]*d*d;
	}
	return(f);
}

void l2r_l2_svc_fun::grad(double *w, double *g)
{
	long long i;
	double *y=prob->y;
	long long l=prob->l;
	long long w_size=get_nr_variable();

	sizeI = 0;
	for (i=0;i<l;i++)
		if (z[i] < 1)
		{
			z[sizeI] = C[i]*y[i]*(z[i]-1);
			I[sizeI] = i;
			sizeI++;
		}
	subXTv(z, g);

	for(i=0;i<w_size;i++)
		g[i] = w[i] + 2*g[i];
}

long long l2r_l2_svc_fun::get_nr_variable(void)
{
	return prob->n;
}

void l2r_l2_svc_fun::Hv(double *s, double *Hs)
{
	long long i;
	long long w_size=get_nr_variable();
	double *wa = new double[sizeI];

	subXv(s, wa);
	for(i=0;i<sizeI;i++)
		wa[i] = C[I[i]]*wa[i];

	subXTv(wa, Hs);
	for(i=0;i<w_size;i++)
		Hs[i] = s[i] + 2*Hs[i];
	delete[] wa;
}

void l2r_l2_svc_fun::Xv(double *v, double *Xv)
{
	long long i;
	long long l=prob->l;
	feature_node **x=prob->x;

	for(i=0;i<l;i++)
	{
		feature_node *s=x[i];
		Xv[i]=0;
		while(s->index!=-1)
		{
			Xv[i]+=v[s->index-1]*s->value;
			s++;
		}
	}
}

void l2r_l2_svc_fun::subXv(double *v, double *Xv)
{
	long long i;
	feature_node **x=prob->x;

	for(i=0;i<sizeI;i++)
	{
		feature_node *s=x[I[i]];
		Xv[i]=0;
		while(s->index!=-1)
		{
			Xv[i]+=v[s->index-1]*s->value;
			s++;
		}
	}
}

void l2r_l2_svc_fun::subXTv(double *v, double *XTv)
{
	long long i;
	long long w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		XTv[i]=0;
	for(i=0;i<sizeI;i++)
	{
		feature_node *s=x[I[i]];
		while(s->index!=-1)
		{
			XTv[s->index-1]+=v[i]*s->value;
			s++;
		}
	}
}

class l2r_l2_svr_fun: public l2r_l2_svc_fun
{
public:
	l2r_l2_svr_fun(const subproblem *prob, double *C, double p);

	double fun(double *w);
	void grad(double *w, double *g);

private:
	double p;
};

l2r_l2_svr_fun::l2r_l2_svr_fun(const subproblem *prob, double *C, double p):
	l2r_l2_svc_fun(prob, C)
{
	this->p = p;
}

double l2r_l2_svr_fun::fun(double *w)
{
	long long i;
	double f=0;
	double *y=prob->y;
	long long l=prob->l;
	long long w_size=get_nr_variable();
	double d;

	Xv(w, z);

	for(i=0;i<w_size;i++)
		f += w[i]*w[i];
	f /= 2;
	for(i=0;i<l;i++)
	{
		d = z[i] - y[i];
		if(d < -p)
			f += C[i]*(d+p)*(d+p);
		else if(d > p)
			f += C[i]*(d-p)*(d-p);
	}

	return(f);
}

void l2r_l2_svr_fun::grad(double *w, double *g)
{
	long long i;
	double *y=prob->y;
	long long l=prob->l;
	long long w_size=get_nr_variable();
	double d;

	sizeI = 0;
	for(i=0;i<l;i++)
	{
		d = z[i] - y[i];

		// generate index set I
		if(d < -p)
		{
			z[sizeI] = C[i]*(d+p);
			I[sizeI] = i;
			sizeI++;
		}
		else if(d > p)
		{
			z[sizeI] = C[i]*(d-p);
			I[sizeI] = i;
			sizeI++;
		}

	}
	subXTv(z, g);

	for(i=0;i<w_size;i++)
		g[i] = w[i] + 2*g[i];
}

// A coordinate descent algorithm for 
// multi-class support vector machines by Crammer and Singer
//
//  min_{\alpha}  0.5 \sum_m ||w_m(\alpha)||^2 + \sum_i \sum_m e^m_i alpha^m_i
//    s.t.     \alpha^m_i <= C^m_i \forall m,i , \sum_m \alpha^m_i=0 \forall i
// 
//  where e^m_i = 0 if y_i  = m,
//        e^m_i = 1 if y_i != m,
//  C^m_i = C if m  = y_i, 
//  C^m_i = 0 if m != y_i, 
//  and w_m(\alpha) = \sum_i \alpha^m_i x_i 
//
// Given: 
// x, y, C
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Appendix of LIBLINEAR paper, Fan et al. (2008)

#define GETI(i) ((long long) prob->y[i])
// To support weights for instances, use GETI(i) (i)

class Solver_MCSVM_CS
{
	public:
		Solver_MCSVM_CS(const problem *prob, long long nr_class, double *C, double eps=0.1, long long max_iter=100000);
		~Solver_MCSVM_CS();
		void Solve(double *w);
	private:
		void solve_sub_problem(double A_i, long long yi, double C_yi, long long active_i, double *alpha_new);
		bool be_shrunk(long long i, long long m, long long yi, double alpha_i, double minG);
		double *B, *C, *G;
		long long w_size, l;
		long long nr_class;
		long long max_iter;
		double eps;
		const problem *prob;
};

Solver_MCSVM_CS::Solver_MCSVM_CS(const problem *prob, long long nr_class, double *weighted_C, double eps, long long max_iter)
{
	this->w_size = prob->n;
	this->l = prob->l;
	this->nr_class = nr_class;
	this->eps = eps;
	this->max_iter = max_iter;
	this->prob = prob;
	this->B = new double[nr_class];
	this->G = new double[nr_class];
	this->C = weighted_C;
}

Solver_MCSVM_CS::~Solver_MCSVM_CS()
{
	delete[] B;
	delete[] G;
}

int compare_double(const void *a, const void *b)
{
	if(*(double *)a > *(double *)b)
		return -1;
	if(*(double *)a < *(double *)b)
		return 1;
	return 0;
}

void Solver_MCSVM_CS::solve_sub_problem(double A_i, long long yi, double C_yi, long long active_i, double *alpha_new)
{
	long long r;
	double *D;

	clone(D, B, active_i);
	if(yi < active_i)
		D[yi] += A_i*C_yi;
	qsort(D, active_i, sizeof(double), compare_double);

	double beta = D[0] - A_i*C_yi;
	for(r=1;r<active_i && beta<r*D[r];r++)
		beta += D[r];
	beta /= r;

	for(r=0;r<active_i;r++)
	{
		if(r == yi)
			alpha_new[r] = min(C_yi, (beta-B[r])/A_i);
		else
			alpha_new[r] = min((double)0, (beta - B[r])/A_i);
	}
	delete[] D;
}

bool Solver_MCSVM_CS::be_shrunk(long long i, long long m, long long yi, double alpha_i, double minG)
{
	double bound = 0;
	if(m == yi)
		bound = C[GETI(i)];
	if(alpha_i == bound && G[m] < minG)
		return true;
	return false;
}

void Solver_MCSVM_CS::Solve(double *w)
{
	long long i, m, s;
	long long iter = 0;
	double *alpha =  new double[l*nr_class];
	double *alpha_new = new double[nr_class];
	long long *index = new long long[l];
	double *QD = new double[l];
	long long *d_ind = new long long[nr_class];
	double *d_val = new double[nr_class];
	long long *alpha_index = new long long[nr_class*l];
	long long *y_index = new long long[l];
	long long active_size = l;
	long long *active_size_i = new long long[l];
	double eps_shrink = max(10.0*eps, 1.0); // stopping tolerance for shrinking
	bool start_from_all = true;

	// Initial alpha can be set here. Note that 
	// sum_m alpha[i*nr_class+m] = 0, for all i=1,...,l-1
	// alpha[i*nr_class+m] <= C[GETI(i)] if prob->y[i] == m
	// alpha[i*nr_class+m] <= 0 if prob->y[i] != m
	// If initial alpha isn't zero, uncomment the for loop below to initialize w
	for(i=0;i<l*nr_class;i++)
		alpha[i] = 0;

	for(i=0;i<w_size*nr_class;i++)
		w[i] = 0;
	for(i=0;i<l;i++)
	{
		for(m=0;m<nr_class;m++)
			alpha_index[i*nr_class+m] = m;
		feature_node *xi = prob->x[i];
		QD[i] = 0;
		while(xi->index != -1)
		{
			double val = xi->value;
			QD[i] += val*val;

			// Uncomment the for loop if initial alpha isn't zero
			// for(m=0; m<nr_class; m++)
			//	w[(xi->index-1)*nr_class+m] += alpha[i*nr_class+m]*val;
			xi++;
		}
		active_size_i[i] = nr_class;
		y_index[i] = (long long)prob->y[i];
		index[i] = i;
	}

	while(iter < max_iter)
	{
		double stopping = -INF;
		for(i=0;i<active_size;i++)
		{
			long long j = i+rand()%(active_size-i);
			swap(index[i], index[j]);
		}
		for(s=0;s<active_size;s++)
		{
			i = index[s];
			double Ai = QD[i];
			double *alpha_i = &alpha[i*nr_class];
			long long *alpha_index_i = &alpha_index[i*nr_class];

			if(Ai > 0)
			{
				for(m=0;m<active_size_i[i];m++)
					G[m] = 1;
				if(y_index[i] < active_size_i[i])
					G[y_index[i]] = 0;

				feature_node *xi = prob->x[i];
				while(xi->index!= -1)
				{
					double *w_i = &w[(xi->index-1)*nr_class];
					for(m=0;m<active_size_i[i];m++)
						G[m] += w_i[alpha_index_i[m]]*(xi->value);
					xi++;
				}

				double minG = INF;
				double maxG = -INF;
				for(m=0;m<active_size_i[i];m++)
				{
					if(alpha_i[alpha_index_i[m]] < 0 && G[m] < minG)
						minG = G[m];
					if(G[m] > maxG)
						maxG = G[m];
				}
				if(y_index[i] < active_size_i[i])
					if(alpha_i[(long long) prob->y[i]] < C[GETI(i)] && G[y_index[i]] < minG)
						minG = G[y_index[i]];

				for(m=0;m<active_size_i[i];m++)
				{
					if(be_shrunk(i, m, y_index[i], alpha_i[alpha_index_i[m]], minG))
					{
						active_size_i[i]--;
						while(active_size_i[i]>m)
						{
							if(!be_shrunk(i, active_size_i[i], y_index[i],
											alpha_i[alpha_index_i[active_size_i[i]]], minG))
							{
								swap(alpha_index_i[m], alpha_index_i[active_size_i[i]]);
								swap(G[m], G[active_size_i[i]]);
								if(y_index[i] == active_size_i[i])
									y_index[i] = m;
								else if(y_index[i] == m)
									y_index[i] = active_size_i[i];
								break;
							}
							active_size_i[i]--;
						}
					}
				}

				if(active_size_i[i] <= 1)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}

				if(maxG-minG <= 1e-12)
					continue;
				else
					stopping = max(maxG - minG, stopping);

				for(m=0;m<active_size_i[i];m++)
					B[m] = G[m] - Ai*alpha_i[alpha_index_i[m]] ;

				solve_sub_problem(Ai, y_index[i], C[GETI(i)], active_size_i[i], alpha_new);
				long long nz_d = 0;
				for(m=0;m<active_size_i[i];m++)
				{
					double d = alpha_new[m] - alpha_i[alpha_index_i[m]];
					alpha_i[alpha_index_i[m]] = alpha_new[m];
					if(fabs(d) >= 1e-12)
					{
						d_ind[nz_d] = alpha_index_i[m];
						d_val[nz_d] = d;
						nz_d++;
					}
				}

				xi = prob->x[i];
				while(xi->index != -1)
				{
					double *w_i = &w[(xi->index-1)*nr_class];
					for(m=0;m<nz_d;m++)
						w_i[d_ind[m]] += d_val[m]*xi->value;
					xi++;
				}
			}
		}

		iter++;
		if(iter % 10 == 0)
		{
			info(".");
		}

		if(stopping < eps_shrink)
		{
			if(stopping < eps && start_from_all == true)
				break;
			else
			{
				active_size = l;
				for(i=0;i<l;i++)
					active_size_i[i] = nr_class;
				info("*");
				eps_shrink = max(eps_shrink/2, eps);
				start_from_all = true;
			}
		}
		else
			start_from_all = false;
	}

	info("\noptimization finished, #iter = %ld\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\n");

	// calculate objective value
	double v = 0;
	long long nSV = 0;
	for(i=0;i<w_size*nr_class;i++)
		v += w[i]*w[i];
	v = 0.5*v;
	for(i=0;i<l*nr_class;i++)
	{
		v += alpha[i];
		if(fabs(alpha[i]) > 0)
			nSV++;
	}
	for(i=0;i<l;i++)
		v -= alpha[i*nr_class+(long long)prob->y[i]];
	info("Objective value = %lf\n",v);
	info("nSV = %ld\n",nSV);

	delete [] alpha;
	delete [] alpha_new;
	delete [] index;
	delete [] QD;
	delete [] d_ind;
	delete [] d_val;
	delete [] alpha_index;
	delete [] y_index;
	delete [] active_size_i;
}

// A coordinate descent algorithm for 
// L1-loss and L2-loss SVM dual problems
//
//  min_\alpha  0.5(\alpha^T (Q + D)\alpha) - e^T \alpha,
//    s.t.      0 <= \alpha_i <= upper_bound_i,
// 
//  where Qij = yi yj xi^T xj and
//  D is a diagonal matrix 
//
// In L1-SVM case:
// 		upper_bound_i = Cp if y_i = 1
// 		upper_bound_i = Cn if y_i = -1
// 		D_ii = 0
// In L2-SVM case:
// 		upper_bound_i = INF
// 		D_ii = 1/(2*Cp)	if y_i = 1
// 		D_ii = 1/(2*Cn)	if y_i = -1
//
// Given: 
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
// 
// See Algorithm 3 of Hsieh et al., ICML 2008

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

static void solve_l2r_l1l2_svc(
	const problem *prob, double *w, double eps,
	double Cp, double Cn, long long solver_type)
{
	long long l = prob->l;
	long long w_size = prob->n;
	long long i, s, iter = 0;
	double C, d, G;
	double *QD = new double[l];
	long long max_iter = 1000;
	long long *index = new long long[l];
	double *alpha = new double[l];
	schar *y = new schar[l];
	long long active_size = l;

	// PG: projected gradient, for shrinking and stopping
	double PG;
	double PGmax_old = INF;
	double PGmin_old = -INF;
	double PGmax_new, PGmin_new;

	// default solver_type: L2R_L2LOSS_SVC_DUAL
	double diag[3] = {0.5/Cn, 0, 0.5/Cp};
	double upper_bound[3] = {INF, 0, INF};
	if(solver_type == L2R_L1LOSS_SVC_DUAL)
	{
		diag[0] = 0;
		diag[2] = 0;
		upper_bound[0] = Cn;
		upper_bound[2] = Cp;
	}

	for(i=0; i<l; i++)
	{
		if(prob->y[i] > 0)
		{
			y[i] = +1;
		}
		else
		{
			y[i] = -1;
		}
	}

	// Initial alpha can be set here. Note that
	// 0 <= alpha[i] <= upper_bound[GETI(i)]
	for(i=0; i<l; i++)
		alpha[i] = 0;

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		QD[i] = diag[GETI(i)];

		feature_node *xi = prob->x[i];
		while (xi->index != -1)
		{
			double val = xi->value;
			QD[i] += val*val;
			w[xi->index-1] += y[i]*alpha[i]*val;
			xi++;
		}
		index[i] = i;
	}

	while (iter < max_iter)
	{
		PGmax_new = -INF;
		PGmin_new = INF;

		for (i=0; i<active_size; i++)
		{
			long long j = i+rand()%(active_size-i);
			swap(index[i], index[j]);
		}

		for (s=0; s<active_size; s++)
		{
			i = index[s];
			G = 0;
			schar yi = y[i];

			feature_node *xi = prob->x[i];
			while(xi->index!= -1)
			{
				G += w[xi->index-1]*(xi->value);
				xi++;
			}
			G = G*yi-1;

			C = upper_bound[GETI(i)];
			G += alpha[i]*diag[GETI(i)];

			PG = 0;
			if (alpha[i] == 0)
			{
				if (G > PGmax_old)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
				else if (G < 0)
					PG = G;
			}
			else if (alpha[i] == C)
			{
				if (G < PGmin_old)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
				else if (G > 0)
					PG = G;
			}
			else
				PG = G;

			PGmax_new = max(PGmax_new, PG);
			PGmin_new = min(PGmin_new, PG);

			if(fabs(PG) > 1.0e-12)
			{
				double alpha_old = alpha[i];
				alpha[i] = min(max(alpha[i] - G/QD[i], 0.0), C);
				d = (alpha[i] - alpha_old)*yi;
				xi = prob->x[i];
				while (xi->index != -1)
				{
					w[xi->index-1] += d*xi->value;
					xi++;
				}
			}
		}

		iter++;
		if(iter % 10 == 0)
			info(".");

		if(PGmax_new - PGmin_new <= eps)
		{
			if(active_size == l)
				break;
			else
			{
				active_size = l;
				info("*");
				PGmax_old = INF;
				PGmin_old = -INF;
				continue;
			}
		}
		PGmax_old = PGmax_new;
		PGmin_old = PGmin_new;
		if (PGmax_old <= 0)
			PGmax_old = INF;
		if (PGmin_old >= 0)
			PGmin_old = -INF;
	}

	info("\noptimization finished, #iter = %ld\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\nUsing -s 2 may be faster (also see FAQ)\n\n");

	// calculate objective value

	double v = 0;
	long long nSV = 0;
	for(i=0; i<w_size; i++)
		v += w[i]*w[i];
	for(i=0; i<l; i++)
	{
		v += alpha[i]*(alpha[i]*diag[GETI(i)] - 2);
		if(alpha[i] > 0)
			++nSV;
	}
	info("Objective value = %lf\n",v/2);
	info("nSV = %ld\n",nSV);

	delete [] QD;
	delete [] alpha;
	delete [] y;
	delete [] index;
}


// A coordinate descent algorithm for 
// L1-loss and L2-loss epsilon-SVR dual problem
//
//  min_\beta  0.5\beta^T (Q + diag(lambda)) \beta - p \sum_{i=1}^l|\beta_i| + \sum_{i=1}^l yi\beta_i,
//    s.t.      -upper_bound_i <= \beta_i <= upper_bound_i,
// 
//  where Qij = xi^T xj and
//  D is a diagonal matrix 
//
// In L1-SVM case:
// 		upper_bound_i = C
// 		lambda_i = 0
// In L2-SVM case:
// 		upper_bound_i = INF
// 		lambda_i = 1/(2*C)
//
// Given: 
// x, y, p, C
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Algorithm 4 of Ho and Lin, 2012   

#undef GETI
#define GETI(i) (0)
// To support weights for instances, use GETI(i) (i)

static void solve_l2r_l1l2_svr(
	const subproblem *prob, double *w, const parameter *param,
	long long solver_type)
{
	long long l = prob->l;
	double C = param->C;
	double p = param->p;
	long long w_size = prob->n;
	double eps = param->eps;
	long long i, s, iter = 0;
	long long max_iter = 1000;
	long long active_size = l;
	long long *index = new long long[l];

	double d, G, H;
	double Gmax_old = INF;
	double Gmax_new, Gnorm1_new;
	double Gnorm1_init;
	double *beta = new double[l];
	double *QD = new double[l];
	double *y = prob->y;

	// L2R_L2LOSS_SVR_DUAL
	double lambda[1], upper_bound[1];
	lambda[0] = 0.5/C;
	upper_bound[0] = INF;

	if(solver_type == L2R_L1LOSS_SVR_DUAL)
	{
		lambda[0] = 0;
		upper_bound[0] = C;
	}

	// Initial beta can be set here. Note that
	// -upper_bound <= beta[i] <= upper_bound
	for(i=0; i<l; i++)
		beta[i] = 0;

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		QD[i] = 0;
		feature_node *xi = prob->x[i];
		while(xi->index != -1)
		{
			double val = xi->value;
			QD[i] += val*val;
			w[xi->index-1] += beta[i]*val;
			xi++;
		}

		index[i] = i;
	}


	while(iter < max_iter)
	{
		Gmax_new = 0;
		Gnorm1_new = 0;

		for(i=0; i<active_size; i++)
		{
			long long j = i+rand()%(active_size-i);
			swap(index[i], index[j]);
		}

		for(s=0; s<active_size; s++)
		{
			i = index[s];
			G = -y[i] + lambda[GETI(i)]*beta[i];
			H = QD[i] + lambda[GETI(i)];

			feature_node *xi = prob->x[i];
			while(xi->index != -1)
			{
				long long ind = xi->index-1;
				double val = xi->value;
				G += val*w[ind];
				xi++;
			}

			double Gp = G+p;
			double Gn = G-p;
			double violation = 0;
			if(beta[i] == 0)
			{
				if(Gp < 0)
					violation = -Gp;
				else if(Gn > 0)
					violation = Gn;
				else if(Gp>Gmax_old && Gn<-Gmax_old)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(beta[i] >= upper_bound[GETI(i)])
			{
				if(Gp > 0)
					violation = Gp;
				else if(Gp < -Gmax_old)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(beta[i] <= -upper_bound[GETI(i)])
			{
				if(Gn < 0)
					violation = -Gn;
				else if(Gn > Gmax_old)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(beta[i] > 0)
				violation = fabs(Gp);
			else
				violation = fabs(Gn);

			Gmax_new = max(Gmax_new, violation);
			Gnorm1_new += violation;

			// obtain Newton direction d
			if(Gp < H*beta[i])
				d = -Gp/H;
			else if(Gn > H*beta[i])
				d = -Gn/H;
			else
				d = -beta[i];

			if(fabs(d) < 1.0e-12)
				continue;

			double beta_old = beta[i];
			beta[i] = min(max(beta[i]+d, -upper_bound[GETI(i)]), upper_bound[GETI(i)]);
			d = beta[i]-beta_old;

			if(d != 0)
			{
				xi = prob->x[i];
				while(xi->index != -1)
				{
					w[xi->index-1] += d*xi->value;
					xi++;
				}
			}
		}

		if(iter == 0)
			Gnorm1_init = Gnorm1_new;
		iter++;
		if(iter % 10 == 0)
			info(".");

		if(Gnorm1_new <= eps*Gnorm1_init)
		{
			if(active_size == l)
				break;
			else
			{
				active_size = l;
				info("*");
				Gmax_old = INF;
				continue;
			}
		}

		Gmax_old = Gmax_new;
	}

	info("\noptimization finished, #iter = %ld\n", iter);
	if(iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\nUsing -s 11 may be faster\n\n");

	// calculate objective value
	double v = 0;
	long long nSV = 0;
	for(i=0; i<w_size; i++)
		v += w[i]*w[i];
	v = 0.5*v;
	for(i=0; i<l; i++)
	{
		v += p*fabs(beta[i]) - y[i]*beta[i] + 0.5*lambda[GETI(i)]*beta[i]*beta[i];
		if(beta[i] != 0)
			nSV++;
	}

	info("Objective value = %lf\n", v);
	info("nSV = %ld\n",nSV);

	delete [] beta;
	delete [] QD;
	delete [] index;
}


// A coordinate descent algorithm for 
// the dual of L2-regularized logistic regression problems
//
//  min_\alpha  0.5(\alpha^T Q \alpha) + \sum \alpha_i log (\alpha_i) + (upper_bound_i - \alpha_i) log (upper_bound_i - \alpha_i),
//    s.t.      0 <= \alpha_i <= upper_bound_i,
// 
//  where Qij = yi yj xi^T xj and 
//  upper_bound_i = Cp if y_i = 1
//  upper_bound_i = Cn if y_i = -1
//
// Given: 
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Algorithm 5 of Yu et al., MLJ 2010

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

void solve_l2r_lr_dual(const problem *prob, double *w, double eps, double Cp, double Cn)
{
	long long l = prob->l;
	long long w_size = prob->n;
	long long i, s, iter = 0;
	double *xTx = new double[l];
	long long max_iter = 1000;
	long long *index = new long long[l];	
	double *alpha = new double[2*l]; // store alpha and C - alpha
	schar *y = new schar[l];
	long long max_inner_iter = 100; // for inner Newton
	double innereps = 1e-2;
	double innereps_min = min(1e-8, eps);
	double upper_bound[3] = {Cn, 0, Cp};

	for(i=0; i<l; i++)
	{
		if(prob->y[i] > 0)
		{
			y[i] = +1;
		}
		else
		{
			y[i] = -1;
		}
	}
	
	// Initial alpha can be set here. Note that
	// 0 < alpha[i] < upper_bound[GETI(i)]
	// alpha[2*i] + alpha[2*i+1] = upper_bound[GETI(i)]
	for(i=0; i<l; i++)
	{
		alpha[2*i] = min(0.001*upper_bound[GETI(i)], 1e-8);
		alpha[2*i+1] = upper_bound[GETI(i)] - alpha[2*i];
	}

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		xTx[i] = 0;
		feature_node *xi = prob->x[i];
		while (xi->index != -1)
		{
			double val = xi->value;
			xTx[i] += val*val;
			w[xi->index-1] += y[i]*alpha[2*i]*val;
			xi++;
		}
		index[i] = i;
	}

	while (iter < max_iter)
	{
		for (i=0; i<l; i++)
		{
			long long j = i+rand()%(l-i);
			swap(index[i], index[j]);
		}
		long long newton_iter = 0;
		double Gmax = 0;
		for (s=0; s<l; s++)
		{
			i = index[s];
			schar yi = y[i];
			double C = upper_bound[GETI(i)];
			double ywTx = 0, xisq = xTx[i];
			feature_node *xi = prob->x[i];
			while (xi->index != -1)
			{
				ywTx += w[xi->index-1]*xi->value;
				xi++;
			}
			ywTx *= y[i];
			double a = xisq, b = ywTx;

			// Decide to minimize g_1(z) or g_2(z)
			long long ind1 = 2*i, ind2 = 2*i+1, sign = 1;
			if(0.5*a*(alpha[ind2]-alpha[ind1])+b < 0)
			{
				ind1 = 2*i+1;
				ind2 = 2*i;
				sign = -1;
			}

			//  g_t(z) = z*log(z) + (C-z)*log(C-z) + 0.5a(z-alpha_old)^2 + sign*b(z-alpha_old)
			double alpha_old = alpha[ind1];
			double z = alpha_old;
			if(C - z < 0.5 * C)
				z = 0.1*z;
			double gp = a*(z-alpha_old)+sign*b+log(z/(C-z));
			Gmax = max(Gmax, fabs(gp));

			// Newton method on the sub-problem
			const double eta = 0.1; // xi in the paper
			long long inner_iter = 0;
			while (inner_iter <= max_inner_iter)
			{
				if(fabs(gp) < innereps)
					break;
				double gpp = a + C/(C-z)/z;
				double tmpz = z - gp/gpp;
				if(tmpz <= 0)
					z *= eta;
				else // tmpz in (0, C)
					z = tmpz;
				gp = a*(z-alpha_old)+sign*b+log(z/(C-z));
				newton_iter++;
				inner_iter++;
			}

			if(inner_iter > 0) // update w
			{
				alpha[ind1] = z;
				alpha[ind2] = C-z;
				xi = prob->x[i];
				while (xi->index != -1)
				{
					w[xi->index-1] += sign*(z-alpha_old)*yi*xi->value;
					xi++;
				}
			}
		}

		iter++;
		if(iter % 10 == 0)
			info(".");

		if(Gmax < eps)
			break;

		if(newton_iter <= l/10)
			innereps = max(innereps_min, 0.1*innereps);

	}

	info("\noptimization finished, #iter = %ld\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\nUsing -s 0 may be faster (also see FAQ)\n\n");

	// calculate objective value

	double v = 0;
	for(i=0; i<w_size; i++)
		v += w[i] * w[i];
	v *= 0.5;
	for(i=0; i<l; i++)
		v += alpha[2*i] * log(alpha[2*i]) + alpha[2*i+1] * log(alpha[2*i+1])
			- upper_bound[GETI(i)] * log(upper_bound[GETI(i)]);
	info("Objective value = %lf\n", v);

	delete [] xTx;
	delete [] alpha;
	delete [] y;
	delete [] index;
}

// A coordinate descent algorithm for 
// L1-regularized L2-loss support vector classification
//
//  min_w \sum |wj| + C \sum max(0, 1-yi w^T xi)^2,
//
// Given: 
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Yuan et al. (2010) and appendix of LIBLINEAR paper, Fan et al. (2008)

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

static void solve_l1r_l2_svc(
	subproblem *prob_col, double *w, double eps,
	double Cp, double Cn)
{
	long long l = prob_col->l;
	long long w_size = prob_col->n;
	long long j, s, iter = 0;
	long long max_iter = 1000;
	long long active_size = w_size;
	long long max_num_linesearch = 20;

	double sigma = 0.01;
	double d, G_loss, G, H;
	double Gmax_old = INF;
	double Gmax_new, Gnorm1_new;
	double Gnorm1_init;
	double d_old, d_diff;
	double loss_old, loss_new;
	double appxcond, cond;

	long long *index = new long long[w_size];
	schar *y = new schar[l];
	double *b = new double[l]; // b = 1-ywTx
	double *xj_sq = new double[w_size];
	feature_node *x;

	double C[3] = {Cn,0,Cp};

	// Initial w can be set here.
	for(j=0; j<w_size; j++)
		w[j] = 0;

	for(j=0; j<l; j++)
	{
		b[j] = 1;
		if(prob_col->y[j] > 0)
			y[j] = 1;
		else
			y[j] = -1;
	}
	for(j=0; j<w_size; j++)
	{
		index[j] = j;
		xj_sq[j] = 0;
		x = prob_col->x[j];
		while(x->index != -1)
		{
			long long ind = x->index-1;
			x->value *= y[ind]; // x->value stores yi*xij
			double val = x->value;
			b[ind] -= w[j]*val;
			xj_sq[j] += C[GETI(ind)]*val*val;
			x++;
		}
	}

	while(iter < max_iter)
	{
		Gmax_new = 0;
		Gnorm1_new = 0;

		for(j=0; j<active_size; j++)
		{
			long long i = j+rand()%(active_size-j);
			swap(index[i], index[j]);
		}

		for(s=0; s<active_size; s++)
		{
			j = index[s];
			G_loss = 0;
			H = 0;

			x = prob_col->x[j];
			while(x->index != -1)
			{
				long long ind = x->index-1;
				if(b[ind] > 0)
				{
					double val = x->value;
					double tmp = C[GETI(ind)]*val;
					G_loss -= tmp*b[ind];
					H += tmp*val;
				}
				x++;
			}
			G_loss *= 2;

			G = G_loss;
			H *= 2;
			H = max(H, 1e-12);

			double Gp = G+1;
			double Gn = G-1;
			double violation = 0;
			if(w[j] == 0)
			{
				if(Gp < 0)
					violation = -Gp;
				else if(Gn > 0)
					violation = Gn;
				else if(Gp>Gmax_old/l && Gn<-Gmax_old/l)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(w[j] > 0)
				violation = fabs(Gp);
			else
				violation = fabs(Gn);

			Gmax_new = max(Gmax_new, violation);
			Gnorm1_new += violation;

			// obtain Newton direction d
			if(Gp < H*w[j])
				d = -Gp/H;
			else if(Gn > H*w[j])
				d = -Gn/H;
			else
				d = -w[j];

			if(fabs(d) < 1.0e-12)
				continue;

			double delta = fabs(w[j]+d)-fabs(w[j]) + G*d;
			d_old = 0;
			long long num_linesearch;
			for(num_linesearch=0; num_linesearch < max_num_linesearch; num_linesearch++)
			{
				d_diff = d_old - d;
				cond = fabs(w[j]+d)-fabs(w[j]) - sigma*delta;

				appxcond = xj_sq[j]*d*d + G_loss*d + cond;
				if(appxcond <= 0)
				{
					x = prob_col->x[j];
					while(x->index != -1)
					{
						b[x->index-1] += d_diff*x->value;
						x++;
					}
					break;
				}

				if(num_linesearch == 0)
				{
					loss_old = 0;
					loss_new = 0;
					x = prob_col->x[j];
					while(x->index != -1)
					{
						long long ind = x->index-1;
						if(b[ind] > 0)
							loss_old += C[GETI(ind)]*b[ind]*b[ind];
						double b_new = b[ind] + d_diff*x->value;
						b[ind] = b_new;
						if(b_new > 0)
							loss_new += C[GETI(ind)]*b_new*b_new;
						x++;
					}
				}
				else
				{
					loss_new = 0;
					x = prob_col->x[j];
					while(x->index != -1)
					{
						long long ind = x->index-1;
						double b_new = b[ind] + d_diff*x->value;
						b[ind] = b_new;
						if(b_new > 0)
							loss_new += C[GETI(ind)]*b_new*b_new;
						x++;
					}
				}

				cond = cond + loss_new - loss_old;
				if(cond <= 0)
					break;
				else
				{
					d_old = d;
					d *= 0.5;
					delta *= 0.5;
				}
			}

			w[j] += d;

			// recompute b[] if line search takes too many steps
			if(num_linesearch >= max_num_linesearch)
			{
				info("#");
				for(long long i=0; i<l; i++)
					b[i] = 1;

				for(long long i=0; i<w_size; i++)
				{
					if(w[i]==0) continue;
					x = prob_col->x[i];
					while(x->index != -1)
					{
						b[x->index-1] -= w[i]*x->value;
						x++;
					}
				}
			}
		}

		if(iter == 0)
			Gnorm1_init = Gnorm1_new;
		iter++;
		if(iter % 10 == 0)
			info(".");

		if(Gnorm1_new <= eps*Gnorm1_init)
		{
			if(active_size == w_size)
				break;
			else
			{
				active_size = w_size;
				info("*");
				Gmax_old = INF;
				continue;
			}
		}

		Gmax_old = Gmax_new;
	}

	info("\noptimization finished, #iter = %ld\n", iter);
	if(iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\n");

	// calculate objective value

	double v = 0;
	long long nnz = 0;
	for(j=0; j<w_size; j++)
	{
		x = prob_col->x[j];
		while(x->index != -1)
		{
			x->value *= prob_col->y[x->index-1]; // restore x->value
			x++;
		}
		if(w[j] != 0)
		{
			v += fabs(w[j]);
			nnz++;
		}
	}
	for(j=0; j<l; j++)
		if(b[j] > 0)
			v += C[GETI(j)]*b[j]*b[j];

	info("Objective value = %lf\n", v);
	info("#nonzeros/#features = %ld/%ld\n", nnz, w_size);

	delete [] index;
	delete [] y;
	delete [] b;
	delete [] xj_sq;
}

// A coordinate descent algorithm for 
// L1-regularized logistic regression problems
//
//  min_w \sum |wj| + C \sum log(1+exp(-yi w^T xi)),
//
// Given: 
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Yuan et al. (2011) and appendix of LIBLINEAR paper, Fan et al. (2008)

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

static void solve_l1r_lr(
	const problem *prob_col, double *w, double eps,
	double Cp, double Cn)
{
	long long l = prob_col->l;
	long long w_size = prob_col->n;
	long long j, s, newton_iter=0, iter=0;
	long long max_newton_iter = 100;
	long long max_iter = 1000;
	long long max_num_linesearch = 20;
	long long active_size;
	long long QP_active_size;

	double nu = 1e-12;
	double inner_eps = 1;
	double sigma = 0.01;
	double w_norm, w_norm_new;
	double z, G, H;
	double Gnorm1_init;
	double Gmax_old = INF;
	double Gmax_new, Gnorm1_new;
	double QP_Gmax_old = INF;
	double QP_Gmax_new, QP_Gnorm1_new;
	double delta, negsum_xTd, cond;

	long long *index = new long long[w_size];
	schar *y = new schar[l];
	double *Hdiag = new double[w_size];
	double *Grad = new double[w_size];
	double *wpd = new double[w_size];
	double *xjneg_sum = new double[w_size];
	double *xTd = new double[l];
	double *exp_wTx = new double[l];
	double *exp_wTx_new = new double[l];
	double *tau = new double[l];
	double *D = new double[l];
	feature_node *x;

	double C[3] = {Cn,0,Cp};

	// Initial w can be set here.
	for(j=0; j<w_size; j++)
		w[j] = 0;

	for(j=0; j<l; j++)
	{
		if(prob_col->y[j] > 0)
			y[j] = 1;
		else
			y[j] = -1;

		exp_wTx[j] = 0;
	}

	w_norm = 0;
	for(j=0; j<w_size; j++)
	{
		w_norm += fabs(w[j]);
		wpd[j] = w[j];
		index[j] = j;
		xjneg_sum[j] = 0;
		x = prob_col->x[j];
		while(x->index != -1)
		{
			long long ind = x->index-1;
			double val = x->value;
			exp_wTx[ind] += w[j]*val;
			if(y[ind] == -1)
				xjneg_sum[j] += C[GETI(ind)]*val;
			x++;
		}
	}
	for(j=0; j<l; j++)
	{
		exp_wTx[j] = exp(exp_wTx[j]);
		double tau_tmp = 1/(1+exp_wTx[j]);
		tau[j] = C[GETI(j)]*tau_tmp;
		D[j] = C[GETI(j)]*exp_wTx[j]*tau_tmp*tau_tmp;
	}

	while(newton_iter < max_newton_iter)
	{
		Gmax_new = 0;
		Gnorm1_new = 0;
		active_size = w_size;

		for(s=0; s<active_size; s++)
		{
			j = index[s];
			Hdiag[j] = nu;
			Grad[j] = 0;

			double tmp = 0;
			x = prob_col->x[j];
			while(x->index != -1)
			{
				long long ind = x->index-1;
				Hdiag[j] += x->value*x->value*D[ind];
				tmp += x->value*tau[ind];
				x++;
			}
			Grad[j] = -tmp + xjneg_sum[j];

			double Gp = Grad[j]+1;
			double Gn = Grad[j]-1;
			double violation = 0;
			if(w[j] == 0)
			{
				if(Gp < 0)
					violation = -Gp;
				else if(Gn > 0)
					violation = Gn;
				//outer-level shrinking
				else if(Gp>Gmax_old/l && Gn<-Gmax_old/l)
				{
					active_size--;
					swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(w[j] > 0)
				violation = fabs(Gp);
			else
				violation = fabs(Gn);

			Gmax_new = max(Gmax_new, violation);
			Gnorm1_new += violation;
		}

		if(newton_iter == 0)
			Gnorm1_init = Gnorm1_new;

		if(Gnorm1_new <= eps*Gnorm1_init)
			break;

		iter = 0;
		QP_Gmax_old = INF;
		QP_active_size = active_size;

		for(long long i=0; i<l; i++)
			xTd[i] = 0;

		// optimize QP over wpd
		while(iter < max_iter)
		{
			QP_Gmax_new = 0;
			QP_Gnorm1_new = 0;

			for(j=0; j<QP_active_size; j++)
			{
				long long i = j+rand()%(QP_active_size-j);
				swap(index[i], index[j]);
			}

			for(s=0; s<QP_active_size; s++)
			{
				j = index[s];
				H = Hdiag[j];

				x = prob_col->x[j];
				G = Grad[j] + (wpd[j]-w[j])*nu;
				while(x->index != -1)
				{
					long long ind = x->index-1;
					G += x->value*D[ind]*xTd[ind];
					x++;
				}

				double Gp = G+1;
				double Gn = G-1;
				double violation = 0;
				if(wpd[j] == 0)
				{
					if(Gp < 0)
						violation = -Gp;
					else if(Gn > 0)
						violation = Gn;
					//inner-level shrinking
					else if(Gp>QP_Gmax_old/l && Gn<-QP_Gmax_old/l)
					{
						QP_active_size--;
						swap(index[s], index[QP_active_size]);
						s--;
						continue;
					}
				}
				else if(wpd[j] > 0)
					violation = fabs(Gp);
				else
					violation = fabs(Gn);

				QP_Gmax_new = max(QP_Gmax_new, violation);
				QP_Gnorm1_new += violation;

				// obtain solution of one-variable problem
				if(Gp < H*wpd[j])
					z = -Gp/H;
				else if(Gn > H*wpd[j])
					z = -Gn/H;
				else
					z = -wpd[j];

				if(fabs(z) < 1.0e-12)
					continue;
				z = min(max(z,-10.0),10.0);

				wpd[j] += z;

				x = prob_col->x[j];
				while(x->index != -1)
				{
					long long ind = x->index-1;
					xTd[ind] += x->value*z;
					x++;
				}
			}

			iter++;

			if(QP_Gnorm1_new <= inner_eps*Gnorm1_init)
			{
				//inner stopping
				if(QP_active_size == active_size)
					break;
				//active set reactivation
				else
				{
					QP_active_size = active_size;
					QP_Gmax_old = INF;
					continue;
				}
			}

			QP_Gmax_old = QP_Gmax_new;
		}

		if(iter >= max_iter)
			info("WARNING: reaching max number of inner iterations\n");

		delta = 0;
		w_norm_new = 0;
		for(j=0; j<w_size; j++)
		{
			delta += Grad[j]*(wpd[j]-w[j]);
			if(wpd[j] != 0)
				w_norm_new += fabs(wpd[j]);
		}
		delta += (w_norm_new-w_norm);

		negsum_xTd = 0;
		for(long long i=0; i<l; i++)
			if(y[i] == -1)
				negsum_xTd += C[GETI(i)]*xTd[i];

		long long num_linesearch;
		for(num_linesearch=0; num_linesearch < max_num_linesearch; num_linesearch++)
		{
			cond = w_norm_new - w_norm + negsum_xTd - sigma*delta;

			for(long long i=0; i<l; i++)
			{
				double exp_xTd = exp(xTd[i]);
				exp_wTx_new[i] = exp_wTx[i]*exp_xTd;
				cond += C[GETI(i)]*log((1+exp_wTx_new[i])/(exp_xTd+exp_wTx_new[i]));
			}

			if(cond <= 0)
			{
				w_norm = w_norm_new;
				for(j=0; j<w_size; j++)
					w[j] = wpd[j];
				for(long long i=0; i<l; i++)
				{
					exp_wTx[i] = exp_wTx_new[i];
					double tau_tmp = 1/(1+exp_wTx[i]);
					tau[i] = C[GETI(i)]*tau_tmp;
					D[i] = C[GETI(i)]*exp_wTx[i]*tau_tmp*tau_tmp;
				}
				break;
			}
			else
			{
				w_norm_new = 0;
				for(j=0; j<w_size; j++)
				{
					wpd[j] = (w[j]+wpd[j])*0.5;
					if(wpd[j] != 0)
						w_norm_new += fabs(wpd[j]);
				}
				delta *= 0.5;
				negsum_xTd *= 0.5;
				for(long long i=0; i<l; i++)
					xTd[i] *= 0.5;
			}
		}

		// Recompute some info due to too many line search steps
		if(num_linesearch >= max_num_linesearch)
		{
			for(long long i=0; i<l; i++)
				exp_wTx[i] = 0;

			for(long long i=0; i<w_size; i++)
			{
				if(w[i]==0) continue;
				x = prob_col->x[i];
				while(x->index != -1)
				{
					exp_wTx[x->index-1] += w[i]*x->value;
					x++;
				}
			}

			for(long long i=0; i<l; i++)
				exp_wTx[i] = exp(exp_wTx[i]);
		}

		if(iter == 1)
			inner_eps *= 0.25;

		newton_iter++;
		Gmax_old = Gmax_new;

		info("iter %3d  #CD cycles %ld\n", newton_iter, iter);
	}

	info("=========================\n");
	info("optimization finished, #iter = %ld\n", newton_iter);
	if(newton_iter >= max_newton_iter)
		info("WARNING: reaching max number of iterations\n");

	// calculate objective value

	double v = 0;
	long long nnz = 0;
	for(j=0; j<w_size; j++)
		if(w[j] != 0)
		{
			v += fabs(w[j]);
			nnz++;
		}
	for(j=0; j<l; j++)
		if(y[j] == 1)
			v += C[GETI(j)]*log(1+1/exp_wTx[j]);
		else
			v += C[GETI(j)]*log(1+exp_wTx[j]);

	info("Objective value = %lf\n", v);
	info("#nonzeros/#features = %ld/%ld\n", nnz, w_size);

	delete [] index;
	delete [] y;
	delete [] Hdiag;
	delete [] Grad;
	delete [] wpd;
	delete [] xjneg_sum;
	delete [] xTd;
	delete [] exp_wTx;
	delete [] exp_wTx_new;
	delete [] tau;
	delete [] D;
}

// transpose matrix X from row format to column format
static void transpose(const subproblem *prob, feature_node **x_space_ret, subproblem *prob_col)
{
	long long i;
	long long l = prob->l;
	long long n = prob->n;
	long long nnz = 0;
	long long *col_ptr = new long long [n+1];
	feature_node *x_space;
	prob_col->l = l;
	prob_col->n = n;
	prob_col->y = new double[l];
	prob_col->x = new feature_node*[n];

	for(i=0; i<l; i++)
		prob_col->y[i] = prob->y[i];

	for(i=0; i<n+1; i++)
		col_ptr[i] = 0;
	for(i=0; i<l; i++)
	{
		feature_node *x = prob->x[i];
		while(x->index != -1)
		{
			nnz++;
			col_ptr[x->index]++;
			x++;
		}
	}
	for(i=1; i<n+1; i++)
		col_ptr[i] += col_ptr[i-1] + 1;

	x_space = new feature_node[nnz+n];
	for(i=0; i<n; i++)
		prob_col->x[i] = &x_space[col_ptr[i]];

	for(i=0; i<l; i++)
	{
		feature_node *x = prob->x[i];
		while(x->index != -1)
		{
			long long ind = x->index-1;
			x_space[col_ptr[ind]].index = i+1; // starts from 1
			x_space[col_ptr[ind]].value = x->value;
			col_ptr[ind]++;
			x++;
		}
	}
	for(i=0; i<n; i++)
		x_space[col_ptr[i]].index = -1;

	*x_space_ret = x_space;

	delete [] col_ptr;
}

// label: label name, start: begin of each class, count: #data of classes, perm: indices to the original data
// perm, length l, must be allocated before calling this subroutine
static void group_classes(const problem *prob, long long *nr_class_ret, long long **label_ret, long long **start_ret, long long **count_ret, long long *perm)
{
	long long l = prob->l;
	long long max_nr_class = 16;
	long long nr_class = 0;
	long long *label = Malloc(long long,max_nr_class);
	long long *count = Malloc(long long,max_nr_class);
//	long long **data_label = Malloc(long long *,l);
	long long i;

	for(i=0;i<l;i++)
	{
		long long jj=0;
		while( jj < prob->numLabels[i] )  {

			long long this_label = (long long)(*(prob->y[i]+jj));

			long long j;
			for(j=0;j<nr_class;j++)
			{
				if(this_label == label[j])
				{
					++count[j];
					break;
				}
			}

			if(j == nr_class)
			{
				if(nr_class == max_nr_class)
				{
					max_nr_class *= 2;
					label = (long long *)realloc(label,max_nr_class*sizeof(long long));
					count = (long long *)realloc(count,max_nr_class*sizeof(long long));
				}
				label[nr_class] = this_label;
				count[nr_class] = 1;
				++nr_class;
			}
			jj++;
		}

	}

	long long *start = Malloc(long long,nr_class);
	*nr_class_ret = nr_class;
	*label_ret = label;
	*start_ret = start;
	*count_ret = count;
}

static void sumVectors(double alpha, double p [],
							double beta, double q [],
							long long n, double *sum ){

	for (long long i =0; i < n; i++){
		sum[i] = alpha*p[i] + beta * q[i];
	}
}

static double computeGradient(const subproblem *prob, double *weight,
								  double *C, double *gradient){

	functionM *fun_obj=NULL;
	double overallObj=0;

	// for squared-hinge loss
	fun_obj=new l1r_l2_svc_prox_fun(prob, C);

	// for smooth-hinge loss
//	fun_obj=new l1r_l2_svc_prox_funS(prob, C);

	//compute the objective and the gradient for squared hinge
	overallObj = fun_obj->fun(weight);
	fun_obj->grad(weight,gradient);

	delete fun_obj;

	return overallObj;

}

static void proximityLasso(double *u, double lambda, double *weight,
		long long numFeats, double *newWeight){

	long long j;

	for (j=0; j < numFeats; j++){
		if(u[j] < 0){
			newWeight[j]= (-1)* std::max(0.0, fabs(u[j])-lambda);
		}
		else{
			newWeight[j] = std::max(0.0, fabs(u[j])-lambda);
		}
	}

//	info("exiting proximity\n");
}

double getGamma(double *weight, double *grad,
		double obj, double initGamma,
		const subproblem *prob, double *C, double lambda){

	int t=0;
	double gamma, gamma_t = 0.1;
	bool gamma_conv = false;
	long long inc = 1;

	long long numParams = prob->n;

	while(!gamma_conv){

		double *gradDummy=Malloc (double,numParams);
		double *u=Malloc (double,numParams);
		double *diff=Malloc (double,numParams);
		double *newWeightVect = Malloc (double,numParams);

		long long ii;

		for (ii =0; ii < numParams; ii++){
			gradDummy[ii] = 0.0; u[ii]=0.0; diff[ii]=0.0;
			newWeightVect[ii]=0.0;
		}

		double minusGamma = -1.0*gamma_t;
		memcpy(u, weight, sizeof(double)*numParams);
		daxpy_(&numParams, &minusGamma, grad, &inc, u, &inc);


		//for Lasso
		proximityLasso(u, gamma_t*lambda, weight, prob->n, newWeightVect);

		double obj_new = computeGradient(prob, newWeightVect, C, gradDummy);

		double minusOne = -1.0;
		memcpy(diff, newWeightVect, sizeof(double)*numParams);
		daxpy_(&numParams, &minusOne, weight, &inc, diff, &inc);
		double term1 = ddot_(&numParams, grad, &inc, diff, &inc);

		double term2 = dnrm2_(&numParams, diff, &inc);
		term2 = (term2*term2)/(2*gamma_t);

		if(obj_new <= (obj + term1 + term2)){
			gamma_conv = true;
		}

		if(!gamma_conv){
			gamma_t = gamma_t/(-initGamma);
			t = t+1;
		}
		free(diff);
		free(newWeightVect);
		free(u);
		free(gradDummy);
	}

	gamma = gamma_t;
	return gamma;
}

static void train_one(const subproblem *prob, const parameter *param, double *w, double Cp, double Cn)
{
	double eps=param->eps;
	long long pos = 0;
	long long neg = 0;
	for(long long i=0;i<prob->l;i++)
		if(prob->y[i] > 0)
			pos++;
	neg = prob->l - pos;

	double primal_solver_tol = eps*max(min(pos,neg), (long long) 1)/prob->l;
	function *fun_obj=NULL;
	switch(param->solver_type)
	{
		case L2R_LR:
		{
			double *C = new double[prob->l];
			for(long long i = 0; i < prob->l; i++)
			{
				if(prob->y[i] > 0)
					C[i] = Cp;
				else
					C[i] = Cn;
			}
			fun_obj=new l2r_lr_fun(prob, C);
			TRON tron_obj(fun_obj, primal_solver_tol);
			tron_obj.set_print_string(liblinear_print_string);
			tron_obj.tron(w);
			delete fun_obj;
			delete[] C;
			break;
		}
		case L2R_L2LOSS_SVC:
		{
			double *C = new double[prob->l];
			for(long long i = 0; i < prob->l; i++)
			{
				if(prob->y[i] > 0)
					C[i] = Cp;
				else
					C[i] = Cn;
			}
			fun_obj=new l2r_l2_svc_fun(prob, C);
			TRON tron_obj(fun_obj, primal_solver_tol);
			tron_obj.set_print_string(liblinear_print_string);
			tron_obj.tron(w);
			delete fun_obj;
			delete[] C;
			break;
		}
		case L2R_L2LOSS_SVC_DUAL:
//			solve_l2r_l1l2_svc(prob, w, eps, Cp, Cn, L2R_L2LOSS_SVC_DUAL);
			break;
		case L2R_L1LOSS_SVC_DUAL:
//			solve_l2r_l1l2_svc(prob, w, eps, Cp, Cn, L2R_L1LOSS_SVC_DUAL);
			break;
		case L1R_L2LOSS_SVC:
		{
			subproblem prob_col;
			feature_node *x_space = NULL;
			transpose(prob, &x_space ,&prob_col);
			solve_l1r_l2_svc(&prob_col, w, primal_solver_tol, Cp, Cn);
			delete [] prob_col.y;
			delete [] prob_col.x;
			delete [] x_space;
			break;
		}
		case L1R_LR:
		{
			problem prob_col;
			feature_node *x_space = NULL;
//			transpose(prob, &x_space ,&prob_col);
			solve_l1r_lr(&prob_col, w, primal_solver_tol, Cp, Cn);
			delete [] prob_col.y;
			delete [] prob_col.x;
			delete [] x_space;
			break;
		}
		case L2R_LR_DUAL:
//			solve_l2r_lr_dual(prob, w, eps, Cp, Cn);
			break;

		case L1R_L2LOSS_SVC_PROX:
		{
			double *C = new double[prob->l];
			for(long long i = 0; i < prob->l; i++)
			{
				if(prob->y[i] > 0)
					C[i] = Cp;
				else
					C[i] = Cn;
			}
			long long inc = 1;

			long long w_size = prob->n;

			double initGamma = -10.0;
			double gamma = 0.01;
			double lambda = 0.1;
			
			double normRatio = 1000;
			double convTh = 0.0001; 

			double *w_prev = Malloc(double, w_size);
			double *diff = Malloc(double, w_size);
			int t = 1;
			double *v = Malloc(double, w_size);

			double *u = Malloc(double, w_size);
			double *grad=Malloc(double, w_size);

			while ((t <= 300) || ((t<= 1800) && (normRatio > convTh))) {
				double obj=0.0;

				for (long long ii =0; ii < w_size; ii++){
					u[ii]=0.0; grad[ii]=0.0;
					diff[ii]=0.0;
					w_prev[ii] = w[ii];
					if(t==1){
						v[ii] = w[ii];
					}
				}
				functionM *fun_obj=NULL;

				// for squared-hinge loss
				fun_obj=new l1r_l2_svc_prox_fun(prob, C);

				// ===========================================//
				//  compute the objective and the gradient
				obj = fun_obj->fun(w);
				fun_obj->grad(w,grad);
				gamma = getGamma(w, grad, obj, initGamma, prob, C, lambda);

				// ===========================================//
				//  take the forward(gradient) step
				double minusGamma = -1.0*gamma;
				memcpy(u, w, sizeof(double)*w_size);
				daxpy_(&w_size, &minusGamma, grad, &inc, u, &inc);


				//  take the backward/proximal step for Lasso
				proximityLasso(u, gamma*lambda, w, w_size, w);


				//evaluate convergence condition
				sumVectors(1.0, w_prev, -1.0, w, w_size, diff);
				double normDiff = dnrm2_(&w_size, diff, &inc);
				double prev_norm = dnrm2_(&w_size, w_prev, &inc);

				normRatio = normDiff/std::max(0.0000000000001, prev_norm);

				delete fun_obj;
				t++;
			}
			info("t is %d\n", t);
			delete fun_obj; free(diff);free(w_prev);free(v);
			free(u);free(grad);
			delete[] C;
			break;
		}


		case L2R_L2LOSS_SVR:
		{
			double *C = new double[prob->l];
			for(long long i = 0; i < prob->l; i++)
				C[i] = param->C;

			fun_obj=new l2r_l2_svr_fun(prob, C, param->p);
			TRON tron_obj(fun_obj, param->eps);
			tron_obj.set_print_string(liblinear_print_string);
			tron_obj.tron(w);
			delete fun_obj;
			delete[] C;
			break;

		}
		case L2R_L1LOSS_SVR_DUAL:
			solve_l2r_l1l2_svr(prob, w, param, L2R_L1LOSS_SVR_DUAL);
			break;
		case L2R_L2LOSS_SVR_DUAL:
			solve_l2r_l1l2_svr(prob, w, param, L2R_L2LOSS_SVR_DUAL);
			break;
		default:
			fprintf(stderr, "ERROR: unknown solver_type\n");
			break;
	}
}

//
// Interface functions
//
model* train(const problem *prob, const parameter *param)
{
	long long i,j;
	long long l = prob->l;
	long long n = prob->n;
	long long w_size = prob->n;
	model *model_ = Malloc(model,1);

	if(prob->bias>=0)
		model_->nr_feature=n-1;
	else
		model_->nr_feature=n;
	model_->param = *param;
	model_->bias = prob->bias;

	if(param->solver_type == L2R_L2LOSS_SVR ||
	   param->solver_type == L2R_L1LOSS_SVR_DUAL ||
	   param->solver_type == L2R_L2LOSS_SVR_DUAL)
	{
		model_->w = Malloc(double, w_size);
		model_->nr_class = 2;
		model_->label = NULL;
//		train_one(prob, param, &model_->w[0], 0, 0);
	}
	else
	{

		long long nr_class = 3786; 

		long long *label = NULL;
		long long *start = NULL;
		long long *count = NULL;
//		long long *perm = Malloc(long long,l);
		long long **labelInd = Malloc(long long *, nr_class);


		//////////////////////////////////

//		printf("number of classes = %d\n", nr_class);
		long long *classCount = Malloc(long long, nr_class);

		label = Malloc(long long, nr_class);

		for(i=0;i<nr_class;i++)
		{
			label[i] = i+1;
			classCount[i]=0;
		}


		for(i=0;i<l;i++)
		{
			long long jj=0;
			while( jj < prob->numLabels[i] )  {
				long long this_label = (long long)(*(prob->y[i]+jj));
				//			printf("%d\n", this_label);
				classCount[this_label-1]++;
				jj++;
			}
		}

		for(i=0;i<nr_class;i++)
		{
//			printf("class Count is class %d count = %d\n", i, classCount[i]);
			labelInd[i] = Malloc(long long , classCount[i]);
		}


		long long *label_ind_ind = Malloc(long long, nr_class);
		for(i=0;i<nr_class;i++)
		{
			label_ind_ind[i] =0;
		}

		for(i=0;i<l;i++)
		{
			long long jj=0;
			while( jj < prob->numLabels[i] )  {
				long long this_label = (long long)(*(prob->y[i]+jj));
				labelInd[this_label-1][label_ind_ind[this_label-1]] = i;
				label_ind_ind[this_label-1]++;
				jj++;
			}
		}

		model_->nr_class=nr_class;
		model_->label = Malloc(long long,nr_class);
		for(i=0;i<nr_class;i++)
			model_->label[i] =label[i];

		// calculate weighted C
		double *weighted_C = Malloc(double, nr_class);
		for(i=0;i<nr_class;i++)
			weighted_C[i] = param->C;
		for(i=0;i<param->nr_weight;i++)
		{
			for(j=0;j<nr_class;j++)
				if(param->weight_label[i] == label[j])
					break;
			if(j == nr_class)
				fprintf(stderr,"WARNING: class label %ld specified in weight is not found\n", param->weight_label[i]);
			else
				weighted_C[j] *= param->weight[i];
		}

		// constructing the subproblem
		feature_node **x = Malloc(feature_node *,l);
		/*for(i=0;i<l;i++)
			x[i] = prob->x[perm[i]];*/

		for(i=0;i<l;i++){
			x[i] = prob->x[i];
		}

		long long k;
		subproblem sub_prob;
		sub_prob.l = l;
		sub_prob.n = n;
		sub_prob.x = Malloc(feature_node *,sub_prob.l);
		sub_prob.y = Malloc(double ,sub_prob.l);

		for(k=0; k<sub_prob.l; k++)
			sub_prob.x[k] = x[k];

		// multi-class svm by Crammer and Singer
		if(param->solver_type == MCSVM_CS)
		{
			/*model_->w=Malloc(double, n*nr_class);
			for(i=0;i<nr_class;i++)
				for(j=start[i];j<start[i]+count[i];j++)
					sub_prob.y[j] = i;
			Solver_MCSVM_CS Solver(&sub_prob, nr_class, weighted_C, param->eps);
			Solver.Solve(model_->w);*/
		}
		else
		{
			if(nr_class == 2)
			{
				/*model_->w=Malloc(double, w_size);

				long long e0 = start[0]+count[0];
				k=0;
				for(; k<e0; k++)
					sub_prob.y[k] = +1;
				for(; k<sub_prob.l; k++)
					sub_prob.y[k] = -1;

				train_one(&sub_prob, param, &model_->w[0], weighted_C[0], weighted_C[1]);*/
			}
			else
			{
				int startInd = 0; int endInd = 0;
				int batch = param->i;
				int batchSize = 1000;
				info("batch is %d\n", batch);
				if(batch == 0){
					startInd = 0;
					endInd = nr_class;
				}
				else{
					startInd = batchSize*(batch-1);
					endInd = startInd + batchSize;
					if(endInd > nr_class){
						batchSize = batchSize-(endInd-nr_class);
						endInd = nr_class;
					}
					info("startInd is %d, endIndex is %d\n", startInd, endInd);
				}
				info("number of classes %d\n ", batchSize);

				//model_->w=Malloc(double, w_size*nr_class);
				model_->w=Malloc(double, w_size*(endInd-startInd));

				model_->nr_class=endInd - startInd;
				model_->label = Malloc(long long,endInd - startInd);
				for(i=0;i<endInd - startInd;i++){
					model_->label[i] = label[startInd+i];
					//	info("labels are %u\n", model_->label[i]);
				}

	//			model_->w=Malloc(double, w_size*nr_class);

	//			double *w=Malloc(double, w_size);
				#pragma omp parallel for private(i)
				for(i=startInd;i<endInd;i++)
				{

					info("started training for %u\n ", model_->label[i%batchSize]);

					subproblem sub_prob_omp;
					sub_prob_omp.l = l;
					sub_prob_omp.n = n;
					sub_prob_omp.x = x;
					sub_prob_omp.y = Malloc(double,l);

					double *w=Malloc(double, w_size);
					for(int j=0;j<w_size;j++){
						w[j] = 0;
					}

					for(k=0; k <sub_prob.l; k ++){
						sub_prob_omp.y[k] = -1;
					}

					long long jj;
					for(jj=0; jj < classCount[i]; jj++){
						long long ind = labelInd[i][jj];
						sub_prob_omp.y[ind] = +1;
					}

					train_one(&sub_prob_omp, param, w, weighted_C[i], param->C);

					info("back from train_one\n");
					for(int j=0;j<w_size;j++){
                                                model_->w[j*batchSize+(i%batchSize)] = w[j];
						w[j] = 0;
					}

/*					for(long long j=0;j<w_size;j++)
						model_->w[j*nr_class+i] = w[j];*/

					free(sub_prob_omp.y);
					free(w);
				}

			}

		}

		free(x);
		free(label);
		free(start);
		free(count);
//		free(perm);
		free(sub_prob.x);
		free(sub_prob.y);
		free(weighted_C);

		free(label_ind_ind);
	}
	return model_;
}

void cross_validation(const subproblem *prob, const parameter *param, long long nr_fold, double *target)
{
	long long i;
	long long *fold_start;
	long long l = prob->l;
	long long *perm = Malloc(long long,l);
	if (nr_fold > l)
	{
		nr_fold = l;
		fprintf(stderr,"WARNING: # folds > # data. Will use # folds = # data instead (i.e., leave-one-out cross validation)\n");
	}
	fold_start = Malloc(long long,nr_fold+1);
	for(i=0;i<l;i++) perm[i]=i;
	for(i=0;i<l;i++)
	{
		long long j = i+rand()%(l-i);
		swap(perm[i],perm[j]);
	}
	for(i=0;i<=nr_fold;i++)
		fold_start[i]=i*l/nr_fold;

	for(i=0;i<nr_fold;i++)
	{
		long long begin = fold_start[i];
		long long end = fold_start[i+1];
		long long j,k;
		struct subproblem subprob;

		subprob.bias = prob->bias;
		subprob.n = prob->n;
		subprob.l = l-(end-begin);
		subprob.x = Malloc(struct feature_node*,subprob.l);
		subprob.y = Malloc(double,subprob.l);

		k=0;
		for(j=0;j<begin;j++)
		{
			subprob.x[k] = prob->x[perm[j]];
			subprob.y[k] = prob->y[perm[j]];
			++k;
		}
		for(j=end;j<l;j++)
		{
			subprob.x[k] = prob->x[perm[j]];
			subprob.y[k] = prob->y[perm[j]];
			++k;
		}
//		struct model *submodel = train(&subprob,param);
//		for(j=begin;j<end;j++)
//			target[perm[j]] = predict(submodel,prob->x[perm[j]]);
//		free_and_destroy_model(&submodel);
		free(subprob.x);
		free(subprob.y);
	}
	free(fold_start);
	free(perm);
}

double predict_values(const struct model *model_, const struct feature_node *x, double *dec_values)
{
	long long idx;
	long long n;
	if(model_->bias>=0)
		n=model_->nr_feature+1;
	else
		n=model_->nr_feature;
	double *w=model_->w;
	long long nr_class=model_->nr_class;
	long long i;
	long long nr_w;
	if(nr_class==2 && model_->param.solver_type != MCSVM_CS)
		nr_w = 1;
	else
		nr_w = nr_class;

	const feature_node *lx=x;
	for(i=0;i<nr_w;i++)
		dec_values[i] = 0;
	for(; (idx=lx->index)!=-1; lx++)
	{
		// the dimension of testing data may exceed that of training
		if(idx<=n)
			for(i=0;i<nr_w;i++)
				dec_values[i] += w[(idx-1)*nr_w+i]*lx->value;
	}

	if(nr_class==2)
	{
		if(model_->param.solver_type == L2R_L2LOSS_SVR ||
		   model_->param.solver_type == L2R_L1LOSS_SVR_DUAL ||
		   model_->param.solver_type == L2R_L2LOSS_SVR_DUAL)
			return dec_values[0];
		else
			return (dec_values[0]>0)?model_->label[0]:model_->label[1];
	}
	else
	{
		long long dec_max_idx = 0;
		for(i=1;i<nr_class;i++)
		{
			if(dec_values[i] > dec_values[dec_max_idx])
				dec_max_idx = i;
		}
		return model_->label[dec_max_idx];
	}
}

long long* maxKIndex(double* array, int arrLength, int top_k) {

	double negative_infinity = - std::numeric_limits<double>::infinity();
	    double *max = Malloc(double, top_k);
	    long long *maxIndex = Malloc(long long, top_k);

	    for(long long i=0; i < top_k;i++){
//	    	printf("%f \n", array[i]);

	    	max[i]= negative_infinity;
	    	maxIndex[i]=-1;
	    }

	    for(long long i = 0; i < arrLength; i++) {
	        for(long long j = 0; j < top_k; j++) {
	            if(array[i] > max[j]) {
	                for(long long x = top_k - 1; x > j; x--) {
	                    maxIndex[x] = maxIndex[x-1]; max[x] = max[x-1];
	                }
	                maxIndex[j] = i; max[j] = array[i];
	                goto top;
	            }
	        }
	        top:
	        ;
	    }

	    for(long long i=0; i < top_k; i++){
	    	printf("%d \n", maxIndex[i]);
	    }
	    return maxIndex;
	}

char** predict_all(const model *model_, const feature_node *x, long long top_k)
{
	double *dec_values = Malloc(double, model_->nr_class);
	double label=predict_values(model_, x, dec_values);

	printf("printing dec_values \n");
	for(long long i=0; i < top_k; i++){
			printf("%f ", dec_values[i]);
		}
	printf("\n");

	char **topKInfo = Malloc(char*, top_k) ;

	long long *topKIndices = Malloc(long long, top_k);
	topKIndices = maxKIndex(dec_values, model_->nr_class, top_k);
	printf("back to predict_all\n");
	for(long long i=0; i < top_k; i++){
		topKInfo[i] = Malloc(char, 200);
		sprintf(topKInfo[i], "%d:%f ",model_->label[topKIndices[i]], dec_values[topKIndices[i]] );
//		printf("%d", topKIndices[i]); printf(":");
//		printf("%f ", dec_values[topKIndices[i]]);
	}
	printf("\n");
	free(topKIndices);
	free(dec_values);
	return topKInfo;
}

double predict(const model *model_, const feature_node *x)
{
	double *dec_values = Malloc(double, model_->nr_class);
	double label=predict_values(model_, x, dec_values);
	free(dec_values);
	return label;
}

double predict_probability(const struct model *model_, const struct feature_node *x, double* prob_estimates)
{
	if(check_probability_model(model_))
	{
		long long i;
		long long nr_class=model_->nr_class;
		long long nr_w;
		if(nr_class==2)
			nr_w = 1;
		else
			nr_w = nr_class;

		double label=predict_values(model_, x, prob_estimates);
		for(i=0;i<nr_w;i++)
			prob_estimates[i]=1/(1+exp(-prob_estimates[i]));

		if(nr_class==2) // for binary classification
			prob_estimates[1]=1.-prob_estimates[0];
		else
		{
			double sum=0;
			for(i=0; i<nr_class; i++)
				sum+=prob_estimates[i];

			for(i=0; i<nr_class; i++)
				prob_estimates[i]=prob_estimates[i]/sum;
		}

		return label;
	}
	else
		return 0;
}

static const char *solver_type_table[]=
{
	"L2R_LR", "L2R_L2LOSS_SVC_DUAL", "L2R_L2LOSS_SVC", "L2R_L1LOSS_SVC_DUAL", "MCSVM_CS",
	"L1R_L2LOSS_SVC", "L1R_LR", "L2R_LR_DUAL",
	"L1R_L2LOSS_SVC_PROX", "", "",
	"L2R_L2LOSS_SVR", "L2R_L2LOSS_SVR_DUAL", "L2R_L1LOSS_SVR_DUAL", NULL
};

long long save_model(const char *model_file_name, const struct model *model_)
{
	long long i;
	long long nr_feature=model_->nr_feature;
	long long n;
	const parameter& param = model_->param;

	if(model_->bias>=0)
		n=nr_feature+1;
	else
		n=nr_feature;
	long long w_size = n;
	FILE *fp = fopen(model_file_name,"w");
	if(fp==NULL) return -1;

	char *old_locale = strdup(setlocale(LC_ALL, NULL));
	setlocale(LC_ALL, "C");

	long long nr_w;
	if(model_->nr_class==2 && model_->param.solver_type != MCSVM_CS)
		nr_w=1;
	else
		nr_w=model_->nr_class;

	fprintf(fp, "solver_type %s\n", solver_type_table[param.solver_type]);
	fprintf(fp, "nr_class %ld\n", model_->nr_class);

	if(model_->label)
	{
		fprintf(fp, "label");
		for(i=0; i<model_->nr_class; i++)
			fprintf(fp, " %ld", model_->label[i]);
		fprintf(fp, "\n");
	}

	fprintf(fp, "nr_feature %ld\n", nr_feature);

	fprintf(fp, "bias %.4g\n", model_->bias);

	fprintf(fp, "w\n");
	for(i=0; i<w_size; i++)
	{
		long long j;
		for(j=0; j<nr_w; j++)
			fprintf(fp, "%.4g ", model_->w[i*nr_w+j]);
		fprintf(fp, "\n");
	}

	setlocale(LC_ALL, old_locale);
	free(old_locale);

	if (ferror(fp) != 0 || fclose(fp) != 0) return -1;
	else return 0;
}

struct model *load_model(const char *model_file_name)
{
	FILE *fp = fopen(model_file_name,"r");
	if(fp==NULL) return NULL;

	long long i;
	long long nr_feature;
	long long n;
	long long nr_class;
	double bias;
	model *model_ = Malloc(model,1);
	parameter& param = model_->param;

	model_->label = NULL;

	char *old_locale = strdup(setlocale(LC_ALL, NULL));
	setlocale(LC_ALL, "C");

	char cmd[81];
	while(1)
	{
		fscanf(fp,"%80s",cmd);
		if(strcmp(cmd,"solver_type")==0)
		{
			fscanf(fp,"%80s",cmd);
			long long i;
			for(i=0;solver_type_table[i];i++)
			{
				if(strcmp(solver_type_table[i],cmd)==0)
				{
					param.solver_type=i;
					break;
				}
			}
			if(solver_type_table[i] == NULL)
			{
				fprintf(stderr,"unknown solver type.\n");

				setlocale(LC_ALL, old_locale);
				free(model_->label);
				free(model_);
				free(old_locale);
				return NULL;
			}
		}
		else if(strcmp(cmd,"nr_class")==0)
		{
			fscanf(fp,"%ld",&nr_class);
			model_->nr_class=nr_class;
		}
		else if(strcmp(cmd,"nr_feature")==0)
		{
			fscanf(fp,"%ld",&nr_feature);
			model_->nr_feature=nr_feature;
		}
		else if(strcmp(cmd,"bias")==0)
		{
			fscanf(fp,"%lf",&bias);
			model_->bias=bias;
		}
		else if(strcmp(cmd,"w")==0)
		{
			break;
		}
		else if(strcmp(cmd,"label")==0)
		{
			long long nr_class = model_->nr_class;
			model_->label = Malloc(long long,nr_class);
			for(long long i=0;i<nr_class;i++)
				fscanf(fp,"%ld",&model_->label[i]);
		}
		else
		{
			fprintf(stderr,"unknown text in model file: [%s]\n",cmd);
			setlocale(LC_ALL, old_locale);
			free(model_->label);
			free(model_);
			free(old_locale);
			return NULL;
		}
	}

	nr_feature=model_->nr_feature;
	if(model_->bias>=0)
		n=nr_feature+1;
	else
		n=nr_feature;
	long long w_size = n;
	long long nr_w;
	if(nr_class==2 && param.solver_type != MCSVM_CS)
		nr_w = 1;
	else
		nr_w = nr_class;

	model_->w=Malloc(double, w_size*nr_w);
	for(i=0; i<w_size; i++)
	{
		long long j;
		for(j=0; j<nr_w; j++)
			fscanf(fp, "%lf ", &model_->w[i*nr_w+j]);
		fscanf(fp, "\n");
	}

	setlocale(LC_ALL, old_locale);
	free(old_locale);

	if (ferror(fp) != 0 || fclose(fp) != 0) return NULL;

	return model_;
}

long long get_nr_feature(const model *model_)
{
	return model_->nr_feature;
}

long long get_nr_class(const model *model_)
{
	return model_->nr_class;
}

void get_labels(const model *model_, long long* label)
{
	if (model_->label != NULL)
		for(long long i=0;i<model_->nr_class;i++)
			label[i] = model_->label[i];
}

void free_model_content(struct model *model_ptr)
{
	if(model_ptr->w != NULL)
		free(model_ptr->w);
	if(model_ptr->label != NULL)
		free(model_ptr->label);
}

void free_and_destroy_model(struct model **model_ptr_ptr)
{
	struct model *model_ptr = *model_ptr_ptr;
	if(model_ptr != NULL)
	{
		free_model_content(model_ptr);
		free(model_ptr);
	}
}

void destroy_param(parameter* param)
{
	if(param->weight_label != NULL)
		free(param->weight_label);
	if(param->weight != NULL)
		free(param->weight);
}

const char *check_parameter(const problem *prob, const parameter *param)
{
	if(param->eps <= 0)
		return "eps <= 0";

	if(param->C <= 0)
		return "C <= 0";

	if(param->p < 0)
		return "p < 0";

	if(param->solver_type != L2R_LR
		&& param->solver_type != L2R_L2LOSS_SVC_DUAL
		&& param->solver_type != L2R_L2LOSS_SVC
		&& param->solver_type != L2R_L1LOSS_SVC_DUAL
		&& param->solver_type != MCSVM_CS
		&& param->solver_type != L1R_L2LOSS_SVC
		&& param->solver_type != L1R_LR
		&& param->solver_type != L2R_LR_DUAL
		&& param->solver_type != L1R_L2LOSS_SVC_PROX
		&& param->solver_type != L2R_L2LOSS_SVR
		&& param->solver_type != L2R_L2LOSS_SVR_DUAL
		&& param->solver_type != L2R_L1LOSS_SVR_DUAL)
		return "unknown solver type";

	return NULL;
}

long long check_probability_model(const struct model *model_)
{
	return (model_->param.solver_type==L2R_LR ||
			model_->param.solver_type==L2R_LR_DUAL ||
			model_->param.solver_type==L1R_LR);
}

void set_print_string_function(void (*print_func)(const char*))
{
	if (print_func == NULL)
		liblinear_print_string = &print_string_stdout;
	else
		liblinear_print_string = print_func;
}


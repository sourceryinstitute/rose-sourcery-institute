template<class T> class X_;
template<class T> X_<T> *g_(X_<T>);

template<class T> class X_
   {
     public:
          X_(T ii, T jj) : i(ii), j(jj) { }
          operator T() { return i + j; }
          friend int f_(void *, int);
          friend X_<T> *g_<>(X_<T>);
          private:
          T i, j;
   };

int f_(void *p, int i)
   {
     switch (i)
        {
          case 0:
             {
               X_<short> *ps = (X_<short>*)p;
               return ps->i;
             }
          case 1:
             {
               X_<int> *pi = (X_<int>*)p;
               return pi->i;
             }
          case 2:
             {
               X_<long> *pl = (X_<long>*)p;
               return int(pl->i);
             }
        }
     return 0;
   }

template<class T> X_<T> *g_(X_<T> r)
   {
     return new X_<T> (r.i + 1, r.j + 1);
   }

void foobar()
   {
     X_<short> si(1, 1);
     X_<int> i(2, 2);
     X_<long> li(4, 4);
     f_(&si, 0);
     f_(&i, 1);
     f_(&li, 2);
     X_<short> *ps = g_(si);
     *ps;
     delete ps;
     X_<int> *pi = g_(i);
     *pi;
     delete pi;
     X_<long> *pl = g_(li);
     *pl;
     delete pl;
   }



#include"my_promise.h"
#include<thread>
#include<iostream>
#include<stdexcept>
#include<exception>
#include <utility>
#include <variant>
#include <atomic>
using namespace mpcs;
using namespace std;


int main()
{
  MyPromise<int> mpi;
  thread thr{ [&]() 
  { 
    auto res = mpi.get_future().get(); // Changed to invariant return type via auto
    
    // Based on the return val of res, overload the lambda called with this result
    std::visit(overloaded{
      [](int v) {std::cout << v << std::endl; },
      [](std::exception_ptr ep)
      { 
        try { std::rethrow_exception(ep); }
        catch(const std::exception &e) { std::cout << e.what() << std::endl; }
      }
  }, res); 
	  
  } };

  // Stays the same
  mpi.set_value(7);
  thr.join();
  return 0;

}

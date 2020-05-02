#include <functional>
#include <set>
#include <vector>
#include "stl_wrapper.h"

struct stl_multiset {
  using Comparator = std::function<bool(void* const &,void* const &)>;
  std::multiset<void*,Comparator> data;
  std::multiset<void*,Comparator>::iterator current;
  std::vector<std::multiset<void*,Comparator>::iterator> saved;
  stl_multiset(Comparator comp) : data(comp), current(data.end()) {}
};

/* The pointer to stl_multiset is stored as a void* in the stl_multiset_t
 * structure (see the header file) because we cannot use C++ types in C code.
 * As a result, all these functions below must cast this pointer back to
 * stl_multiset* with reinterpret_cast.
 */

extern "C" {

void stlMultisetNew(stl_multiset_t* c, cups_array_func_t comp)
{
  if (c == NULL)
    return;
  auto comp2 = [comp](void* a, void *b)->bool {return (comp(a,b,NULL) < 0);};
  c->ptr = new stl_multiset(comp2);
}

void stlMultisetDelete(stl_multiset_t* c)
{
  if (c == NULL)
    return;
  delete reinterpret_cast<stl_multiset*>(c->ptr);
  c->ptr = NULL;
}

void stlMultisetAdd(stl_multiset_t c, void* element)
{
  if (c.ptr == NULL)
    return;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  cc->current = cc->data.insert(element);
}

int stlMultisetCount(stl_multiset_t c) {
  if (c.ptr == NULL)
    return 0;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  return cc->data.size();
}

void* stlMultisetFind(stl_multiset_t c, void* key)
{
  if (c.ptr == NULL)
    return NULL;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  auto iters = cc->data.equal_range(key);
  if (iters.first == iters.second) {
    cc->current = cc->data.end();
    return NULL;
  }
  cc->current = iters.first;
  return *(cc->current);
}

void* stlMultisetRemove(stl_multiset_t c, void* key)
{
  if (c.ptr == NULL)
    return NULL;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  auto iters = cc->data.equal_range(key);
  if (iters.first == iters.second) {
    cc->current = cc->data.end();
    return NULL;
  }
  cc->current = iters.first;
  ++(cc->current);
  auto elem = *(iters.first);
  cc->data.erase(iters.first);
  return elem;
}

void* stlMultisetFirst(stl_multiset_t c)
{
  if (c.ptr == NULL)
    return NULL;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  cc->current = cc->data.begin();
  if (cc->current == cc->data.end())
    return NULL;
  return *(cc->current);
}

void* stlMultisetNext(stl_multiset_t c)
{
  if (c.ptr == NULL)
    return NULL;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  if (cc->current == cc->data.end())
    return NULL;
  ++(cc->current);
  if (cc->current == cc->data.end())
    return NULL;
  return *(cc->current);
}

void stlMultisetIndexEnd(stl_multiset_t c)
{
  if (c.ptr == NULL)
    return;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  cc->current = cc->data.end();
}

int stlMultisetSave(stl_multiset_t c)
{
  if (c.ptr == NULL)
    return 0;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  if (cc->current == cc->data.end())
    return 0;
  cc->saved.push_back(cc->current);
  return 1;
}

void* stlMultisetRestore(stl_multiset_t c)
{
  if (c.ptr == NULL)
    return NULL;
  stl_multiset *cc = reinterpret_cast<stl_multiset*>(c.ptr);
  if (cc->saved.empty())
    return NULL;
  cc->current = cc->saved.back();
  cc->saved.pop_back();
  return *(cc->current);
}

}

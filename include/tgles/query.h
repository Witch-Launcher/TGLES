#ifndef TGLES_QUERY_H
#define TGLES_QUERY_H

// Asynchronous query objects (spec 4.2, Table 4.2): exactly the four core
// targets verified in test_query_targets.cpp. GPU completion is modeled by
// CompleteQuery (the backend calls it when results land, see step 9).

#include <map>

#include "tgles/error.h"
#include "tgles/gl_types.h"
#include "tgles/spec.h"  // QueryTarget ground truth.

namespace tgles {

inline constexpr GLenum kGlCurrentQuery = 0x8865;
inline constexpr GLenum kGlQueryResult = 0x8866;
inline constexpr GLenum kGlQueryResultAvailable = 0x8867;

class QueryManager {
 public:
  QueryManager();

  GLenum GetError();
  bool HasPending() const;

  void GenQueries(GLsizei n, GLuint* ids);
  void DeleteQueries(GLsizei n, const GLuint* ids);
  GLboolean IsQuery(GLuint id);
  void BeginQuery(GLenum target, GLuint id);
  void EndQuery(GLenum target);
  void GetQueryiv(GLenum target, GLenum pname, GLint* params);
  void GetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params);

  // Test/backend helper: deliver a result as if the GPU finished.
  void CompleteQuery(GLuint id, GLuint result);

 private:
  struct Query {
    bool alive = false;
    GLenum target = 0;
    bool result_available = false;
    GLuint result = 0;
  };

  bool IsCoreTarget(GLenum target) const;
  Query* Find(GLuint id);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Query> queries_;
  std::map<GLenum, GLuint> active_;  // target -> id (0 = none).
};

}  // namespace tgles

#endif  // TGLES_QUERY_H

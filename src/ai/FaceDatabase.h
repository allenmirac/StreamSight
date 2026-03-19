// FaceDatabase.h
// In-memory face feature library for registration and similarity search.
// Persists to / loads from a JSON file on disk.
//
// Thread safety: All public methods are protected by an internal mutex.

#ifndef AI_FACE_DATABASE_H
#define AI_FACE_DATABASE_H

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace ai {

/** @brief One registered person. */
struct FaceEntry {
    std::string        name;       ///< Person identifier / name
    std::vector<float> embedding;  ///< 512-D L2-normalized ArcFace embedding
};

/** @brief Result of a database lookup. */
struct FaceMatch {
    std::string name;       ///< Matched name, or "unknown"
    float       similarity; ///< Cosine similarity to best match
    bool        matched;    ///< True if similarity >= threshold
};

/**
 * @brief Stores and queries face embeddings.
 *
 * Usage:
 *   FaceDatabase db("faces.json");
 *   db.Load();
 *   db.Register("Alice", embedding);
 *   FaceMatch m = db.Query(probe_embedding);
 */
class FaceDatabase {
public:
    /**
     * @param db_path   Path to JSON database file (created if absent).
     * @param threshold Minimum cosine similarity for a positive match (default 0.4).
     */
    explicit FaceDatabase(const std::string& db_path = "faces.json",
                          float threshold = 0.4f);

    /** @brief Load entries from disk. Returns number of entries loaded. */
    int Load();

    /** @brief Save all entries to disk. */
    bool Save() const;

    /**
     * @brief Register a new person (or overwrite if name exists).
     * @param name      Person identifier.
     * @param embedding L2-normalized 512-D embedding.
     */
    void Register(const std::string& name, const std::vector<float>& embedding);

    /** @brief Remove a person by name. Returns true if found. */
    bool Remove(const std::string& name);

    /**
     * @brief Find closest match for a query embedding.
     * @param embedding  L2-normalized probe embedding.
     * @return FaceMatch (matched=false and name="unknown" if below threshold).
     */
    FaceMatch Query(const std::vector<float>& embedding) const;

    /** @brief Return list of all registered names. */
    std::vector<std::string> ListNames() const;

    /** @brief Number of registered entries. */
    size_t Size() const;

private:
    std::string              db_path_;
    float                    threshold_;
    mutable std::mutex       mutex_;
    std::vector<FaceEntry>   entries_;
};

} // namespace ai

#endif // AI_FACE_DATABASE_H

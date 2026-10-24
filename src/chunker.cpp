#include "inc/base.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>

std::vector<std::string> TextChunker::chunk_string(
    const std::string& text, 
    size_t max_words,
    size_t overlap) 
{
    std::vector<std::string> chunks;
    std::istringstream stream(text);
    std::vector<std::string> words;
    std::string word;

    while (stream >> word) {
        words.push_back(word);
    }
    if (words.empty()) {
        return chunks;
    }

    size_t step = (max_words > overlap) ? (max_words - overlap) : max_words;

    for (size_t i = 0; i < words.size(); i += step) {
        std::string chunk_text;
        size_t end = std::min(i + max_words, words.size());
        for (size_t j = i; j < end; ++j) {
            chunk_text += words[j] + (j + 1 == end ? "" : " ");
        }
        chunks.push_back(std::move(chunk_text));
        if (end == words.size()) {
            break;
        }
    }
    return chunks;
}

std::vector<std::string> TextChunker::parse_and_chunk_pdf(
    const std::string& filepath,
    size_t max_words,
    size_t overlap) 
{
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return {};
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execlp("pdftotext", "pdftotext", filepath.c_str(), "-", nullptr);
        _exit(127);
    }

    // Parent process
    close(pipefd[1]);
    std::string text;
    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        text += buffer;
    }

    close(pipefd[0]);
    waitpid(pid, nullptr, 0);

    return chunk_string(text, max_words, overlap);
}

std::vector<std::string> TextChunker::chunk_file(
    const std::string& path,
    size_t max_words,
    size_t overlap) 
{
    if (path.empty() || access(path.c_str(), F_OK) != 0) {
        return {};
    }
    if (path.ends_with(".pdf")) {
        return parse_and_chunk_pdf(path, max_words, overlap);
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return chunk_string(ss.str(), max_words, overlap);
}
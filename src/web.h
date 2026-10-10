#pragma once
void webBegin();
void webLoop();
// Serial api mode (spec 7b, console.cpp): runs one request line through the web API's handlers; the
// answer goes out as JSON lines (apiOut)
void webApiRequest(const char* line);

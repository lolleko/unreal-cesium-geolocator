﻿// Fill out your copyright notice in the Description page of Project Settings.

#include "MTOverpassQuery.h"

#include "HttpModule.h"

#include "GenericPlatform/GenericPlatformHttp.h"

#include "Interfaces/IHttpResponse.h"

namespace
{
    void AsyncQueryInternal(
        const FString& OverpassQueryString,
        const FMTOverPassQueryCompletionDelegate& CompletionDelegate,
        const int32 CurrentTry)
    {
        constexpr auto MaxRetries = 3;
        if (CurrentTry >= MaxRetries)
        {
            CompletionDelegate.ExecuteIfBound({}, false);

            return;
        }
        FHttpModule& HTTP = FHttpModule::Get();
        
        const auto QueryRequest = HTTP.CreateRequest();
        QueryRequest->SetVerb(TEXT("GET"));

        const auto EscapedUrl = FString::Printf(TEXT("http://www.overpass-api.de/api/interpreter?data=%s"), *FGenericPlatformHttp::UrlEncode(OverpassQueryString.Replace(TEXT("\n"), TEXT(""))));
        QueryRequest->SetURL(EscapedUrl);

        QueryRequest->OnProcessRequestComplete().BindLambda(
            [&CompletionDelegate, CurrentTry, OverpassQueryString](
                FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
            {
                if (bConnectedSuccessfully)
                {
                    // if we receive html it means we got an error from overpass API
                    if (Response->GetContentType().Contains(TEXT("text/html")))
                    {
                        // TODO restart reequest a couple of times
                        AsyncQueryInternal(OverpassQueryString, CompletionDelegate, CurrentTry + 1);
                    }
                    else
                    {
                        const auto Content = Response->GetContentAsString();
                        AsyncTask(
                            ENamedThreads::AnyBackgroundThreadNormalTask,
                            [Content, &CompletionDelegate]()
                            {
                                FOverPassQueryResult Result;
                                FJsonObjectConverter::JsonObjectStringToUStruct(Content, &Result);

                                AsyncTask(
                                    ENamedThreads::GameThread,
                                    [Result, &CompletionDelegate]()
                                    { CompletionDelegate.ExecuteIfBound(Result, true); });
                            });
                    }
                }
                else
                {
                    switch (Request->GetStatus())
                    {
                        case EHttpRequestStatus::Failed_ConnectionError:
                            UE_LOG(LogTemp, Error, TEXT("Connection failed."));
                        default:
                            UE_LOG(LogTemp, Error, TEXT("Request failed."));
                    }
                }
            });

        QueryRequest->ProcessRequest();
    }
}  // namespace

void MTOverpass::AsyncQuery(const FString& OverpassQueryString, const FMTOverPassQueryCompletionDelegate& CompletionDelegate)
{
    AsyncQueryInternal(OverpassQueryString, CompletionDelegate, 0);
}

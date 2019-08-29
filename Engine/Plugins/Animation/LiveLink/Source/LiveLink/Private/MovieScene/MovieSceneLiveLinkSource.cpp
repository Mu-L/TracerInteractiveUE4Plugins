// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneLiveLinkSource.h"
#include "Features/IModularFeatures.h"

FMovieSceneLiveLinkSource::FMovieSceneLiveLinkSource():
	Client(nullptr)
	, LastFramePublished(0)
{
}

TSharedPtr<FMovieSceneLiveLinkSource> FMovieSceneLiveLinkSource::CreateLiveLinkSource(const FName& SubjectName)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		TSharedPtr <FMovieSceneLiveLinkSource> Source = MakeShareable(new FMovieSceneLiveLinkSource());
		LiveLinkClient->AddSource(Source);
		LiveLinkClient->AddSourceToSubjectWhiteList(SubjectName, Source->SourceGuid);
		return Source;
	}
	return TSharedPtr<FMovieSceneLiveLinkSource>();
}

void FMovieSceneLiveLinkSource::RemoveLiveLinkSource(TSharedPtr<FMovieSceneLiveLinkSource> Source, const FName& SubjectName)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkClient->RemoveSourceFromSubjectWhiteList(SubjectName, Source->SourceGuid);
		LiveLinkClient->RemoveSource(Source);
	}
}

void FMovieSceneLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
}

bool FMovieSceneLiveLinkSource::IsSourceStillValid()
{
	return Client != nullptr;
}

bool FMovieSceneLiveLinkSource::RequestSourceShutdown()
{
	Client = nullptr;
	return true;
}

FText FMovieSceneLiveLinkSource::GetSourceMachineName() const
{
	return FText().FromString(FPlatformProcess::ComputerName());
}

FText FMovieSceneLiveLinkSource::GetSourceStatus() const
{
	return NSLOCTEXT( "MovieSceneLiveLinkSource", "MovieSceneLiveLinkSourceStatus", "Active" );
}

FText FMovieSceneLiveLinkSource::GetSourceType() const
{
	return FText::Format(NSLOCTEXT("FMovieSceneLiveLinkSource", "MovieSceneLiveLinkSourceType", "Sequencer Live Link ({0})"),FText::FromName(LastSubjectName));
}

void FMovieSceneLiveLinkSource::PublishLiveLinkFrameData(const FName& SubjectName, const TArray<FLiveLinkFrameData>& LiveLinkFrameDataArray, const FLiveLinkRefSkeleton& RefSkeleton)
{
	check(Client != nullptr);
	if (SubjectName != LastSubjectName)
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			ILiveLinkClient* LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
			if (LastSubjectName.IsValid())
			{
				LiveLinkClient->RemoveSourceFromSubjectWhiteList(LastSubjectName, SourceGuid);
			}
			LiveLinkClient->AddSourceToSubjectWhiteList(SubjectName, SourceGuid);
		}

		// We need to publish a skeleton for this subject name even though we doesn't use one
		Client->PushSubjectSkeleton(SourceGuid, SubjectName, RefSkeleton);
	}
	LastSubjectName = SubjectName;
	for (FLiveLinkFrameData LiveLinkFrame : LiveLinkFrameDataArray)
	{
		// Share the data locally with the LiveLink client
		Client->PushSubjectData(SourceGuid, SubjectName, LiveLinkFrame);
	}
}


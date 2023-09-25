// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class GeolocatorTarget : TargetRules
{
	public GeolocatorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;

		bBuildAdditionalConsoleApp = true;

		ExtraModuleNames.AddRange( new string[] { "Geolocator" } );
	}
}

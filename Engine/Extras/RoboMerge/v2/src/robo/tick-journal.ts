// Copyright Epic Games, Inc. All Rights Reserved.

export interface TickJournal {
	merges: number
	conflicts: number
	integrationErrors: number
	syntaxErrors: number

	monitored: boolean
}

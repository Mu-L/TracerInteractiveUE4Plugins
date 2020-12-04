// Copyright Epic Games, Inc. All Rights Reserved.

import { BranchSpec, ConflictedResolveNFile, RoboWorkspace } from '../common/perforce';
// TODO: Remove Circular Dependency on bot-interfaces
import { NodeBotInterface, QueuedChange } from './bot-interfaces';
import { BotConfig, BranchBase, EdgeOptions, NodeOptions } from './branchdefs';
import { BlockageNodeOpUrls } from './roboserver';
import { PauseStatusFields } from './state-interfaces';
import { ConflictStatusFields } from './conflicts';

export type FailureKind =
	'Integration error' |
	'Exclusive check-out' |
	'Merge conflict' |
	'Commit failure' |
	'Syntax error' |
	'Disallowed files' |
	'Too many files' |
	'Conversion to edits failure' |
	'Unit Test error'

export type BranchArg = Branch | string
export function resolveBranchArg(branchArg: BranchArg, toUpperCase?: boolean): string {
	return toUpperCase ? 
		(branchArg as Branch).upperName || (branchArg as string).toUpperCase() :
		(branchArg as Branch).name || branchArg as string
}
export function branchesMatch(branchArg1: BranchArg | null | undefined, branchArg2: BranchArg | null | undefined) {
	return !branchArg1 || !branchArg2 || (typeof(branchArg1) === 'object' && typeof(branchArg2) === 'object') ?
		branchArg1 === branchArg2 : resolveBranchArg(branchArg1, true) === resolveBranchArg(branchArg2, true)
}

// Interface for returning from non-trivial node operations
export interface OperationResult { 
	success: boolean
	message: string // Goal: to provide meaningful error messaging to the end user
}

export interface StompedRevision {
	changelist: string
	author: string
	description: string
}

export interface StompVerificationFile {
	filetype: string // output from p4resolve for display purposes
	resolved: boolean // Determine if the file was successfully resolved and does not need to be stomped

	resolveResult?: ConflictedResolveNFile // output data from "p4 -ztag resolve -N"

	// Stomped Revisions can have a few returns
	// 1. stomped revisions were able to be calculated, and stompedRevisions is a populated array (can be empty)
	// 2. stomped revision calculations were skipped due to some criteria (such as task streams)
	// 3. we were not able to determine stomped revisions for some reason
	stompedRevisions?: StompedRevision[] | null // revisions being stomped from common ancestor between branches
	stompedRevisionsSkipped: boolean
	stompedRevisionsCalculationIssues: boolean
	
	targetFileName : string

	branchOrDeleteResolveRequired: boolean // Currently we don't have a good way to handle branch / delete merges. Fail those requests for now.
}

export interface StompVerification extends OperationResult {
	validRequest?: boolean
	nonBinaryFilesResolved?: boolean // Warning to the user that non-binary files are included in changelist, but passed the first resolve
	svFiles?: StompVerificationFile[] // Array of remaining files

	// Convinence booleans to alert user to problems in verification result
	branchOrDeleteResolveRequired?: boolean // Currently we don't have a good way to handle branch / delete merges. Fail those requests for now.
	remainingAllBinary?: boolean // Check to see if unresolved non-binary files are remaining -- we shouldn't stomp those!
}

export interface BranchGraphInterface {
	botname: string
	config: BotConfig

	branches: Branch[]

	getBranchesMonitoringSameStreamAs(branch: Branch): Branch[] | null

	// todo: make a better utility for this
	_computeReachableFrom(visited: Set<Branch>, flowKey: string, branch: Branch): Set<string>

	getBranch(name: string): Branch | undefined
	getBranchNames(): string
}

type BotStatusFields = Partial<PauseStatusFields> & {
	display_name: string
	last_cl: number

	is_active: boolean
	is_available: boolean
	is_blocked: boolean
	is_paused: boolean

	status_msg?: string
	status_since?: string

	lastBlockage?: number

	disallowSkip: boolean

	// don't commit - added by preprocess
	retry_cl?: number
}

export type EdgeStatusFields = BotStatusFields & {
	name: string
	target: string
	targetStream?: string
	rootPath: string

	headCL?: number
	lastGoodCL?: number
	lastGoodCLJobLink?: string
	lastGoodCLDate?: Date
}

type NodeStatusFields = BotStatusFields & {
		
	queue: QueuedChange[]
	headCL?: number

	conflicts: ConflictStatusFields[]
	edges: { [key: string]: EdgeStatusFields }
}

export type BranchStatus = Partial<NodeStatusFields> & {
	def: Branch
	bot: string

	branch_spec_cl: number
}

export interface Branch extends BranchBase {
	bot?: NodeBotInterface
	parent: BranchGraphInterface
	workspace: RoboWorkspace

	branchspec: Map<string, BranchSpec>
	upperName: string
	depot: string
	stream?: string
	config: NodeOptions
	reachable?: string[]
	forcedDownstream?: string[]
	enabled: boolean
	convertIntegratesToEdits: boolean
	visibility: string[] | string
	blockAssetTargets: Set<string>

	edgeProperties: Map<string, EdgeOptions>

	isMonitored: boolean // property
}

export interface Target {
	branch: Branch
	mergeMode: string
}

export type MergeMode = 'safe' | 'normal' | 'null' | 'clobber' | 'skip'
export interface MergeAction {
	branch: Branch
	mergeMode: MergeMode
	furtherMerges: Target[]
	flags: Set<string>
	description?: string  // gets filled in for immediate targets in _mergeCl
}

export interface TargetInfo {
	errors?: string[]
	allDownstream?: Branch[]

	targets?: MergeAction[]

	owner?: string
	author: string

	targetWorkspaceForShelf?: string // Filled in during the reconsider in case of a createShelf nodeop request
	sendNoShelfEmail: boolean // Used for internal use shelves, such as stomp changes

	forceStompChanges: boolean
	additionalDescriptionText?: string
}

export interface ChangeInfo extends TargetInfo {
	branch: Branch
	cl: number
	source_cl: number
	isManual: boolean
	authorTag?: string
	source: string
	description: string
	numFiles: number // number of files, capped out at maxFilesPerIntegration

	propagatingNullMerge: boolean
	forceCreateAShelf: boolean
	overriddenCommand: string
	hasOkForGithubTag: boolean
}

export interface PendingChange {
	change: ChangeInfo
	action: MergeAction
	newCl: number
}

export interface Failure {
	kind: FailureKind		// short description of integration error or conflict
	description: string		// detailed description (can be very long - don't want to store this)
	summary?: string
}

export interface Blockage {
	action: MergeAction | null // If we have a syntax error, this can be null
	change: ChangeInfo
	failure: Failure
	owner: string
	ownerEmail: Promise<string | null>

	time: Date
}
export type NodeOpUrlGenerator = (blockage: Blockage | null) => BlockageNodeOpUrls | null


export interface ConflictingFile {
	name: string
	kind: ConflictKind
}
export type ConflictKind = 'merge' | 'branch' | 'delete' | 'unknown'

export interface AlreadyIntegrated {
	change: ChangeInfo
	action: MergeAction
}

export interface ForcedCl {
	nodeOrEdgeName: string
	forcedCl: number
	previousCl: number
	culprit: string
	reason: string
}
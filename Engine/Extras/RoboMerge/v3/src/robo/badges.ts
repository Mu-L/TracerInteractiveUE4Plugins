// Copyright Epic Games, Inc. All Rights Reserved.

import { Badge } from '../common/badge';
import { ContextualLogger } from '../common/logger';
import { PerforceContext } from '../common/perforce';
import { Blockage, Branch, BranchGraphInterface, ChangeInfo } from './branch-interfaces';
import { PersistentConflict, Resolution } from './conflict-interfaces';
import { BotEventHandler, BotEvents } from './events';

const BADGE_LABEL = 'Merge'

//										 -   1  -        -----    2   -----
const CHANGE_INFO_VIA_REGEX = /CL \d+ in (\/\/.*)\/\.\.\.((?: via CL \d+)*)/

//													   - 1-            - 2-                      - 3-
const BOT_DESCRIPTION_LINE_REGEX = /#ROBOMERGE-BOT:?\s*(\w+)\s*\(([-a-zA-Z0-9_\.]+)\s*->\s*([-a-zA-Z0-9_\.]+)\)/

type BadgeFunc = (cl: number, stream: string) => void

function isChangeUpstreamFromBadgeProject(change: ChangeInfo) {
	if (change.branch.badgeProject) {
		// for now, checking strictly upstream; could have a flag
		return false
	}

	if (change.allDownstream) {
		for (const branch of change.allDownstream) {
			if (branch.badgeProject) {
				return true
			}
		}
	}

	return false
}



class BadgeHandler implements BotEventHandler {
	public readonly p4: PerforceContext

	constructor(badgeBranch: Branch, private allBranchStreams: Map<string, string>,
		externalUrl : string, parentLogger: ContextualLogger) {
		// for now, single badge project
		// possibility: could check multiple. WOuld need to be prioritised for consistency
		this.badgeProject = badgeBranch.badgeProject!
		// this.badgeLabel = `Merge:->${badgeBranch.name}`
		this.botname = badgeBranch.parent.botname

		this.externalUrl = externalUrl

		this.badgeHandlerLogger = parentLogger.createChild('BadgeHandler')
		this.p4 = new PerforceContext(this.badgeHandlerLogger)
	}

	/** See if this change implies badges should be added or updated */
	onChangeParsed(info: ChangeInfo) {

		// make sure it's a robo-merge to the branch, not a normal commit
		if (info.cl !== info.source_cl && info.branch.badgeProject) {
			const result = info.propagatingNullMerge ? Badge.SKIPPED : Badge.SUCCESS

			if (!this.markIntegrationChain(info, result)) {
				this.badgeHandlerLogger.warn('unable to parse source of suspected RM CL: ' + info.source)
			}
		}
	}

	private markInProgressCl(info: ChangeInfo, result: string) {
		Badge.mark(result, BADGE_LABEL, `${info.branch.stream}/${this.badgeProject}`, info.cl, this.botname, this.externalUrl)
	}

	onBlockage(blockage: Blockage) {
		const change = blockage.change

		if (!change.isManual && isChangeUpstreamFromBadgeProject(change)) {
			this.markInProgressCl(change, Badge.FAILURE)
		}
	}

	onBranchUnblocked(info: PersistentConflict) {
		// don't know if this lead to a badge project. Not much harm in having the odd false positive here
		const stream = this.allBranchStreams.get(info.blockedBranchName)
		if (stream) {
			const result = info.resolution === Resolution.RESOLVED || info.resolution === Resolution.DUNNO ? Badge.SUCCESS : Badge.SKIPPED
			Badge.mark(result, BADGE_LABEL, `${stream}/${this.badgeProject}`, info.cl, this.botname, this.externalUrl)
		}
	}

	onConflictStatus(anyConflicts: boolean) {
		const status = anyConflicts ? Badge.FAILURE : Badge.SUCCESS
		for (const stream of this.allBranchStreams.values()) {
			Badge.mark(status, 'RoboMerge', `${stream}/${this.badgeProject}`, 0, this.botname, this.externalUrl)
		}
	}

	/** On successful (possibly null) merge, mark whole chain (but not target branch) with same status */
	private markIntegrationChain(info: ChangeInfo, result: string) {
		const match = info.source.match(CHANGE_INFO_VIA_REGEX)
		if (!match) {
			return false
		}

		const badgeFunc = (cl: number, stream: string) =>
							Badge.mark(result, BADGE_LABEL, `${stream}/${this.badgeProject}`, cl, this.botname, this.externalUrl)

		const sourceStream = match[1]
		badgeFunc(info.source_cl, sourceStream)

		// vias!
		const allVias = match[2]
		if (allVias) {
			// note that at the point CL that commits to badge project is included in the vias
			// (may not actually be committed anywhere else)
			const viaClStrs = allVias.split(' via CL ')

			// this will look up each changelist sequentially - no need to wait for it
			// (maybe should technically wait for it at some level - could theoretically start marking a different status
			// in parallel otherwise)
			markViaBranches(viaClStrs.slice(1, viaClStrs.length - 1), badgeFunc, info.branch.parent, this.p4, this.badgeHandlerLogger)
		}
		return true
	}

	private badgeHandlerLogger : ContextualLogger
	private botname: string
	private badgeProject: string
	private externalUrl: string
}

/** Add badges to branches listed as 'via' */
async function markViaBranches(viaClStrings: string[], badgeFunc: BadgeFunc, branchGraph: BranchGraphInterface, p4: PerforceContext, logger: ContextualLogger) {
	for (const viaClStr of viaClStrings) {
		const cl = parseInt(viaClStr)
		if (isNaN(cl)) {
			logger.warn('failed to parse via CL: ' + viaClStr)
			continue
		}

		let result
		try {
			result = await p4.getChange(`//${branchGraph.config.defaultStreamDepot}/...`, cl)
		}
		catch (err) {
			// this can fail sometimes with cross depot integrations
			logger.warn('Failed to find changelist')
			continue
		}

		for (const line of result.desc.split('\n')) {
			const botLineMatch = line.match(BOT_DESCRIPTION_LINE_REGEX)
			if (botLineMatch) {
				// 1: bot, 2: origin, 3: stream committed to
				const branchName = botLineMatch[3]
				const branch = branchGraph.getBranch(branchName)
				if (!branch) {
					logger.warn('branch not found to add UGS badge to: ' + branchName)
				}
				else if (branch.stream) {
					// only adding badges to stream projects at the moment
					badgeFunc(cl, branch.stream)
				}
			}
		}
	}
}

export function bindBadgeHandler(events: BotEvents, branchGraph: BranchGraphInterface, externalUrl: string, logger: ContextualLogger) {

	let badgeBranch: Branch | null = null

	for (const branch of branchGraph.branches) {
		if (branch.config.badgeProject) {
			if (branch.stream) {
				badgeBranch = branch
			}
			else {
				logger.info(`Branch '${branch.name}' set as badge branch but isn't a stream`)
			}
			break
		}
	}

	// badges enabled if badge branch set in config
	if (badgeBranch) {
		const allStreams = new Map<string, string>()
		for (const bm of branchGraph.branches) {
			if (bm.stream) {
				allStreams.set(bm.upperName, bm.stream)
			}
		}
		events.registerHandler(new BadgeHandler(badgeBranch, allStreams, externalUrl, logger))
	}
}

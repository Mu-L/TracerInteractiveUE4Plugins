// Copyright Epic Games, Inc. All Rights Reserved.
import { ContextualLogger, NpmLogLevel } from "../common/logger";
import { Change, ChangelistStatus, PerforceContext } from "../common/perforce";
import { BotIPC, ReconsiderArgs } from "./bot-interfaces";
import { Branch, OperationResult, PendingChange } from "./branch-interfaces";
import { Context } from "./settings";
import { BlockagePauseInfo, BlockagePauseInfoMinimal, PauseState } from "./state-interfaces";

export interface PerforceRequestResult {
	changes?: Change[] | null
	errors: any[]
}

export abstract class PerforceStatefulBot implements BotIPC {
	protected p4: PerforceContext
	abstract branch: Branch
	private _lastClProcessed: number

	protected lastAction = ''
	protected actionStart = new Date

	// transient, for information only
	protected lastBlockage = -1

	abstract isActive: boolean

	isRunning = false
	readonly pauseState: PauseState

	constructor(protected readonly settings: Context, initialCl?: number) {
		this.pauseState = new PauseState((reason: string) => { this.unblock(reason) }, settings)
		this._lastClProcessed = this.settings.getInt('lastCl', initialCl || 0)

		if (this.pauseState.isBlocked()) {
			const blockageCL = this.pauseState.blockagePauseInfo!.change
			if (blockageCL) {
				this.lastBlockage = blockageCL
			}
		}
	}

	get isManuallyPaused() {
		return this.pauseState.isManuallyPaused()
	}
	get isBlocked() {
		return this.pauseState.isBlocked()
	}
	get isAvailable() {
		return this.pauseState.isAvailable()
	}
	
	block(blockageInfo: BlockagePauseInfoMinimal, pauseDurationSeconds?: number) {
		if (blockageInfo.change) {
			this.lastBlockage = blockageInfo.change 
		}

		if (this.isBlocked) {
			return
		}

		// updates pauseInfo endsAt
		this.pauseState.block(blockageInfo, pauseDurationSeconds)

		const info = this.pauseState.infoStr.replace(/\n/g, ' / ')
		this.logger.info(`Blocking ${this.fullName}. ${info}. Ends ${(blockageInfo as BlockagePauseInfo).endsAt || 'never'}`)
	}
	unblock(reason: string) {
		if (!this.isBlocked) {
			return
		}

		this.pauseState.unblock()
		this.logger.info(`Unblocked ${this.fullName} due to ${reason}.`)
	}

	pause(message: string, owner: string) {
		if (this.isManuallyPaused) {
			return
		}

		this.pauseState.manuallyPause(message, owner)
		this.logger.info(`Paused ${this.fullName}: ${message}.`)
	}
	unpause() {
		if (!this.isManuallyPaused) {
			return
		}

		this.pauseState.unpause()
		this.logger.info(`Unpaused ${this.fullName}.`)
	}

	abstract reconsider(instigator: string, changeCl: number, additionalArgs?: Partial<ReconsiderArgs>): void

	acknowledge(acknowledger: string, changeCl: number) : OperationResult {
		if (!this.isBlocked) {
			return { success: false, message: "Cannot acknowledge a bot that is not currently blocked" }
		}

		return this.acknowledgeConflict(acknowledger, changeCl, this.pauseState,  this.pauseState.blockagePauseInfo!)
	}
	abstract acknowledgeConflict(acknowledger : string, changeCl : number, pauseState: PauseState, blockageInfo: BlockagePauseInfo) : OperationResult

	unacknowledge(changeCl : number) : OperationResult {
		if (!this.isBlocked) {
			return { success: false, message: "Cannot unacknowledge a bot that is not currently blocked" }
		}

		return this.unacknowledgeConflict(changeCl, this.pauseState, this.pauseState.blockagePauseInfo!)
	}
	abstract unacknowledgeConflict(changeCl : number, pauseState: PauseState, blockageInfo: BlockagePauseInfo) : OperationResult

	protected static getIntegrationOwner(targetBranch: Branch, overriddenOwner?: string): string | null
	protected static getIntegrationOwner(pending: PendingChange): string
	protected static getIntegrationOwner(arg0: Branch | PendingChange, overriddenOwner?: string) {
		// order of priority for owner:

		//  1) manual requester - if this change was manually requested, use the requestor (now set as the change owner)
		//	2) resolver - need resolver to take priority, even over reconsider, since recon might be from branch with
		//					multiple targets, so instigator might not even know about target with resolver
		//	3) reconsider
		//	4) propagated/manually added tag
		//	5) author - return null here for that case

		const pending = (arg0 as PendingChange)

		const branch = pending.action ? pending.action.branch : (arg0 as Branch)
		const owner = pending.change ? pending.change.owner : overriddenOwner

		// Manual requester
		if ( (pending.action && pending.action.flags.has('manual')) ||
			(pending.change && pending.change.forceCreateAShelf)
		) {
			return owner
		}
		return branch!.resolver || owner || null
	}

	abstract get displayName(): string
	abstract get fullName(): string
	abstract get fullNameForLogging(): string
	protected abstract get logger(): ContextualLogger

	get lastCl() {
		return this._lastClProcessed;
	}
	set lastCl(value: number) {
		// make sure it's always increasing
		if (this.lastCl && value < this.lastCl) {
			return;
		}

		// set the cache and save it
		this._forceSetLastCl_NoReset(value);
	}
	protected forceSetLastCl(value: number) {
		const prevValue = this._forceSetLastCl_NoReset(value)

		// When we force set the last CL, we want to unblock
		this.unblock(`${this.fullNameForLogging} last CL forcibly set to ${value}`)

		return prevValue
	}
	protected _forceSetLastCl_NoReset(value: number): number {
		if (value > this.lastBlockage) {
			this.lastBlockage = -1
		}
		let prevValue = this.lastCl
		this._lastClProcessed = value
		this.settings.set("lastCl", value)
		return prevValue
	}
	static async setBotToLatestClInBranch(bot: PerforceStatefulBot, sourceBranch: Branch) {
		bot._log_action("Getting starting CL");
		
		let change: Change;
		try {
			change = await bot.p4.latestChange(sourceBranch.rootPath);
		}
		catch (err) {
			bot.logger.printException(err, `${bot.fullName} Error while querying P4 for changes`);
			return;
		}

		if (!change)
			throw new Error(`Unable to query for changes in: ${sourceBranch.rootPath}`);

		// set the most recent change
		bot.lastCl = change.change;
	}

	abstract forceSetLastClWithContext(value: number, culprit: string, reason: string): number;

	async _getChange(changeCl: number, path?: string, status?: ChangelistStatus) : Promise<PerforceRequestResult> {
		let change: Change | null = null
		let errors: any[] = []
		try {
			change = await this.p4.getChange(path || this.branch.rootPath, changeCl, status)
		}
		catch (err) {
			return { errors: [err] }
		}

		return { changes: [change], errors }
	}

	protected _log_action(action: string, logLevel: NpmLogLevel = 'info') {
		this.logger[logLevel](action)
		
		this.lastAction = action;
		this.actionStart = new Date;
	}

	async start() {
		if (this.isRunning)
			throw new Error("already running");

		// log and start
		this.isRunning = true;

		if (this.lastCl <= 0) {
			await this.setBotToLatestCl()

			// start ticking
			this.logger.info(`Began monitoring ${this.fullName} at CL ${this.lastCl} (INITIAL)`);
		}
		else {
			// start ticking
			this.logger.info(`Began monitoring ${this.fullName} at CL ${this.lastCl}`);
		}
	}
	/**
	 * You can use the helper function PerforceStatefulBot.setBotToLatestClInBranch to accomplish this.
	 */
	abstract async setBotToLatestCl(): Promise<void>

	abstract async tick(): Promise<boolean>;
}

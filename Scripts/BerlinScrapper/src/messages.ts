export interface Message {
    type: MessageType;
}

export enum MessageType {
    Init = "Init",
    Completion = "Completion",
    ScrapeURL = "ScrapeURL",
    QueueURL = "QueueURL",
}

export interface InitMessage extends Message {
    type: MessageType.Init;
    cdn: "a.3d.blc.shc.eu" | "b.3d.blc.shc.eu" | "c.3d.blc.shc.eu" | "d.3d.blc.shc.eu";
    baseRegex: RegExp;

    accessToken: string;

    fixGLTF: boolean;
}

export function isInitMessage(message: Message): message is InitMessage {
    return message.type === MessageType.Init;
}

export interface CompletionMessage extends Message {
    type: MessageType.Completion;
    bytesWritten: number;
    filePath: string;
    wasFileCreated: boolean;
}

export function isCompletionMessage(message: Message): message is CompletionMessage {
    return message.type === MessageType.Completion;
}

export interface QueueURLMessage extends Message {
    type: MessageType.QueueURL;
    url: string;
    
}

export function isQueueURLMessage(message: Message): message is QueueURLMessage {
    return message.type === MessageType.QueueURL;
}

export interface ScrapeURLMessage extends Message {
    type: MessageType.ScrapeURL;
    url: string;
    
}
export function isScrapeURLMessage(message: Message): message is ScrapeURLMessage {
    return message.type === MessageType.ScrapeURL;
}
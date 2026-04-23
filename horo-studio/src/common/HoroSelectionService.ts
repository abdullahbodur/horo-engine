import { injectable } from '@theia/core/shared/inversify';
import { Emitter, Event } from '@theia/core';

/**
 * Shared selection state for Horo Studio panels.
 * SceneHierarchyWidget writes selectedId; InspectorWidget subscribes.
 */
@injectable()
export class HoroSelectionService {
  private _selectedId: string | undefined;
  private readonly onSelectionChangedEmitter = new Emitter<string | undefined>();

  readonly onSelectionChanged: Event<string | undefined> = this.onSelectionChangedEmitter.event;

  get selectedId(): string | undefined {
    return this._selectedId;
  }

  select(id: string | undefined): void {
    if (this._selectedId === id) return;
    this._selectedId = id;
    this.onSelectionChangedEmitter.fire(id);
  }
}

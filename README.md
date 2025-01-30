# olifilo

## esp::idf::events

This implementation uses per-subscription IPC message queues to collect
subscribed-to events. Notification of messages in these can be done by
select() like other file descriptors via the VFS integration which
delivers the event messages via its read() call.

The 'events::subscriber' class on top of that handles decoding into:

    std::pair<std::variant<event_id_types...>, std::variant<event_payload_types...>>

For convenience:

1. All types in these variants are deduplicated
2. All event id types and payload types are sorted on `tuple{sort_key_for(event_base), event_id}`
3. `void` payloads are replaced by 'std::monostate'
    - which is sorted to the front
4. This pair will never contain variants with one (1) alternative only
    - they're replaced by that alternative in that case (without
      wrapping variant)
    - if, and only if, subscribing to exactly one event the return type
      is the associated payload type (without any wrapping pair & variants)
    - if the only payload type is `void`, and subscribing to more than 1
      event, the `pair` is omitted and what would have been its
      `first_type` is returned instead.

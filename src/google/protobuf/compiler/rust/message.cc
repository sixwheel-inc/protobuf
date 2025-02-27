// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "google/protobuf/compiler/rust/message.h"

#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/compiler/cpp/helpers.h"
#include "google/protobuf/compiler/cpp/names.h"
#include "google/protobuf/compiler/rust/accessors/accessors.h"
#include "google/protobuf/compiler/rust/context.h"
#include "google/protobuf/compiler/rust/naming.h"
#include "google/protobuf/compiler/rust/oneof.h"
#include "google/protobuf/descriptor.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace rust {
namespace {

void MessageNew(Context<Descriptor> msg) {
  switch (msg.opts().kernel) {
    case Kernel::kCpp:
      msg.Emit({{"new_thunk", Thunk(msg, "new")}}, R"rs(
        Self { inner: $pbr$::MessageInner { msg: unsafe { $new_thunk$() } } }
      )rs");
      return;

    case Kernel::kUpb:
      msg.Emit({{"new_thunk", Thunk(msg, "new")}}, R"rs(
        let arena = $pbr$::Arena::new();
        Self {
          inner: $pbr$::MessageInner {
            msg: unsafe { $new_thunk$(arena.raw()) },
            arena,
          }
        }
      )rs");
      return;
  }

  ABSL_LOG(FATAL) << "unreachable";
}

void MessageSerialize(Context<Descriptor> msg) {
  switch (msg.opts().kernel) {
    case Kernel::kCpp:
      msg.Emit({{"serialize_thunk", Thunk(msg, "serialize")}}, R"rs(
        unsafe { $serialize_thunk$(self.inner.msg) }
      )rs");
      return;

    case Kernel::kUpb:
      msg.Emit({{"serialize_thunk", Thunk(msg, "serialize")}}, R"rs(
        let arena = $pbr$::Arena::new();
        let mut len = 0;
        unsafe {
          let data = $serialize_thunk$(self.inner.msg, arena.raw(), &mut len);
          $pbr$::SerializedData::from_raw_parts(arena, data, len)
        }
      )rs");
      return;
  }

  ABSL_LOG(FATAL) << "unreachable";
}

void MessageDeserialize(Context<Descriptor> msg) {
  switch (msg.opts().kernel) {
    case Kernel::kCpp:
      msg.Emit(
          {
              {"deserialize_thunk", Thunk(msg, "deserialize")},
          },
          R"rs(
          let success = unsafe {
            let data = $pbr$::SerializedData::from_raw_parts(
              $NonNull$::new(data.as_ptr() as *mut _).unwrap(),
              data.len(),
            );

            $deserialize_thunk$(self.inner.msg, data)
          };
          success.then_some(()).ok_or($pb$::ParseError)
        )rs");
      return;

    case Kernel::kUpb:
      msg.Emit({{"deserialize_thunk", Thunk(msg, "parse")}}, R"rs(
        let arena = $pbr$::Arena::new();
        let msg = unsafe {
          $deserialize_thunk$(data.as_ptr(), data.len(), arena.raw())
        };

        match msg {
          None => Err($pb$::ParseError),
          Some(msg) => {
            // This assignment causes self.arena to be dropped and to deallocate
            // any previous message pointed/owned to by self.inner.msg.
            self.inner.arena = arena;
            self.inner.msg = msg;
            Ok(())
          }
        }
      )rs");
      return;
  }

  ABSL_LOG(FATAL) << "unreachable";
}

void MessageExterns(Context<Descriptor> msg) {
  switch (msg.opts().kernel) {
    case Kernel::kCpp:
      msg.Emit(
          {
              {"new_thunk", Thunk(msg, "new")},
              {"delete_thunk", Thunk(msg, "delete")},
              {"serialize_thunk", Thunk(msg, "serialize")},
              {"deserialize_thunk", Thunk(msg, "deserialize")},
          },
          R"rs(
          fn $new_thunk$() -> $pbi$::RawMessage;
          fn $delete_thunk$(raw_msg: $pbi$::RawMessage);
          fn $serialize_thunk$(raw_msg: $pbi$::RawMessage) -> $pbr$::SerializedData;
          fn $deserialize_thunk$(raw_msg: $pbi$::RawMessage, data: $pbr$::SerializedData) -> bool;
        )rs");
      return;

    case Kernel::kUpb:
      msg.Emit(
          {
              {"new_thunk", Thunk(msg, "new")},
              {"serialize_thunk", Thunk(msg, "serialize")},
              {"deserialize_thunk", Thunk(msg, "parse")},
          },
          R"rs(
          fn $new_thunk$(arena: $pbi$::RawArena) -> $pbi$::RawMessage;
          fn $serialize_thunk$(msg: $pbi$::RawMessage, arena: $pbi$::RawArena, len: &mut usize) -> $NonNull$<u8>;
          fn $deserialize_thunk$(data: *const u8, size: usize, arena: $pbi$::RawArena) -> Option<$pbi$::RawMessage>;
      )rs");
      return;
  }

  ABSL_LOG(FATAL) << "unreachable";
}

void MessageDrop(Context<Descriptor> msg) {
  if (msg.is_upb()) {
    // Nothing to do here; drop glue (which will run drop(self.arena)
    // automatically) is sufficient.
    return;
  }

  msg.Emit({{"delete_thunk", Thunk(msg, "delete")}}, R"rs(
    unsafe { $delete_thunk$(self.inner.msg); }
  )rs");
}

void GetterForViewOrMut(Context<FieldDescriptor> field, bool is_mut) {
  auto fieldName = field.desc().name();
  auto fieldType = field.desc().type();
  auto getter_thunk = Thunk(field, "get");
  auto setter_thunk = Thunk(field, "set");
  auto clearer_thunk = Thunk(field, "clear");
  // If we're dealing with a Mut, the getter must be supplied
  // self.inner.msg() whereas a View has to be supplied self.msg
  auto self = is_mut ? "self.inner.msg()" : "self.msg";

  if (fieldType == FieldDescriptor::TYPE_MESSAGE) {
    Context<Descriptor> d = field.WithDesc(field.desc().message_type());
    // TODO: support messages which are defined in other crates.
    if (!IsInCurrentlyGeneratingCrate(d)) {
      return;
    }
    auto prefix = "crate::" + GetCrateRelativeQualifiedPath(d);
    field.Emit(
        {
            {"prefix", prefix},
            {"field", fieldName},
            {"self", self},
            {"getter_thunk", getter_thunk},
            // TODO: dedupe with singular_message.cc
            {
                "view_body",
                [&] {
                  if (field.is_upb()) {
                    field.Emit({}, R"rs(
                      let submsg = unsafe { $getter_thunk$($self$) };
                      match submsg {
                        None => $prefix$View::new($pbi$::Private,
                          $pbr$::ScratchSpace::zeroed_block($pbi$::Private)),
                        Some(field) => $prefix$View::new($pbi$::Private, field),
                      }
                )rs");
                  } else {
                    field.Emit({}, R"rs(
                      let submsg = unsafe { $getter_thunk$($self$) };
                      $prefix$View::new($pbi$::Private, submsg)
                )rs");
                  }
                },
            },
        },
        R"rs(
              pub fn r#$field$(&self) -> $prefix$View {
                $view_body$
              }
            )rs");
    return;
  }

  auto rsType = PrimitiveRsTypeName(field.desc());
  if (fieldType == FieldDescriptor::TYPE_STRING) {
    field.Emit({{"field", fieldName},
                {"self", self},
                {"getter_thunk", getter_thunk},
                {"setter_thunk", setter_thunk},
                {"RsType", rsType},
                {"maybe_mutator",
                 [&] {
                   if (is_mut) {
                     field.Emit({}, R"rs(
                    pub fn r#$field$_mut(&self) -> $pb$::Mut<'_, $RsType$> {
                       static VTABLE: $pbi$::BytesMutVTable = 
                        $pbi$::BytesMutVTable::new(
                          $pbi$::Private,
                          $getter_thunk$,
                          $setter_thunk$,
                        );

                       unsafe {
                        <$pb$::Mut<$RsType$>>::from_inner(
                          $pbi$::Private,
                          $pbi$::RawVTableMutator::new(
                            $pbi$::Private,
                            self.inner,
                            &VTABLE,
                           )
                        )
                      }
                    }
                    )rs");
                   }
                 }}},
               R"rs(
              pub fn r#$field$(&self) -> $pb$::View<'_, $RsType$> {
                let s = unsafe { $getter_thunk$($self$).as_ref() };
                unsafe { __pb::ProtoStr::from_utf8_unchecked(s) }
              }

              $maybe_mutator$
            )rs");
  } else if (fieldType == FieldDescriptor::TYPE_BYTES) {
    field.Emit(
        {
            {"field", fieldName},
            {"self", self},
            {"getter_thunk", getter_thunk},
            {"RsType", rsType},
        },
        R"rs(
              pub fn r#$field$(&self) -> $pb$::View<'_, $RsType$> {
                unsafe { $getter_thunk$($self$).as_ref() }
              }
            )rs");
  } else {
    field.Emit({{"field", fieldName},
                {"getter_thunk", getter_thunk},
                {"setter_thunk", setter_thunk},
                {"clearer_thunk", clearer_thunk},
                {"self", self},
                {"RsType", rsType},
                {"maybe_mutator",
                 [&] {
                   // TODO: once the rust public api is accessible,
                   // by tooling, ensure that this only appears for the
                   // mutational pathway
                   if (is_mut && fieldType) {
                     field.Emit({}, R"rs(
                    pub fn r#$field$_mut(&self) -> $pb$::Mut<'_, $RsType$> {
                      static VTABLE: $pbi$::PrimitiveVTable<$RsType$> =
                        $pbi$::PrimitiveVTable::new(
                          $pbi$::Private,
                          $getter_thunk$,
                          $setter_thunk$);
                      $pb$::PrimitiveMut::from_inner(
                        $pbi$::Private,
                        unsafe {
                          $pbi$::RawVTableMutator::new($pbi$::Private,
                          self.inner,
                          &VTABLE)
                        })
                    }
                    )rs");
                   }
                 }}},
               R"rs(
            pub fn r#$field$(&self) -> $pb$::View<'_, $RsType$> {
              unsafe { $getter_thunk$($self$) }
            }

            $maybe_mutator$
          )rs");
  }
}

void AccessorsForViewOrMut(Context<Descriptor> msg, bool is_mut) {
  for (int i = 0; i < msg.desc().field_count(); ++i) {
    auto field = msg.WithDesc(*msg.desc().field(i));
    if (field.desc().is_repeated()) continue;
    // TODO - add cord support
    if (field.desc().options().has_ctype()) continue;
    // TODO
    if (field.desc().type() == FieldDescriptor::TYPE_ENUM ||
        field.desc().type() == FieldDescriptor::TYPE_GROUP)
      continue;
    GetterForViewOrMut(field, is_mut);
    msg.printer().PrintRaw("\n");
  }
}

}  // namespace

void GenerateRs(Context<Descriptor> msg) {
  if (msg.desc().map_key() != nullptr) {
    ABSL_LOG(WARNING) << "unsupported map field: " << msg.desc().full_name();
    return;
  }
  msg.Emit(
      {{"Msg", msg.desc().name()},
       {"Msg::new", [&] { MessageNew(msg); }},
       {"Msg::serialize", [&] { MessageSerialize(msg); }},
       {"Msg::deserialize", [&] { MessageDeserialize(msg); }},
       {"Msg::drop", [&] { MessageDrop(msg); }},
       {"Msg_externs", [&] { MessageExterns(msg); }},
       {"accessor_fns",
        [&] {
          for (int i = 0; i < msg.desc().field_count(); ++i) {
            auto field = msg.WithDesc(*msg.desc().field(i));
            msg.Emit({{"comment", FieldInfoComment(field)}}, R"rs(
                 // $comment$
               )rs");
            GenerateAccessorMsgImpl(field);
            msg.printer().PrintRaw("\n");
          }
        }},
       {"oneof_accessor_fns",
        [&] {
          for (int i = 0; i < msg.desc().real_oneof_decl_count(); ++i) {
            GenerateOneofAccessors(
                msg.WithDesc(*msg.desc().real_oneof_decl(i)));
            msg.printer().PrintRaw("\n");
          }
        }},
       {"accessor_externs",
        [&] {
          for (int i = 0; i < msg.desc().field_count(); ++i) {
            GenerateAccessorExternC(msg.WithDesc(*msg.desc().field(i)));
            msg.printer().PrintRaw("\n");
          }
        }},
       {"oneof_externs",
        [&] {
          for (int i = 0; i < msg.desc().real_oneof_decl_count(); ++i) {
            GenerateOneofExternC(msg.WithDesc(*msg.desc().real_oneof_decl(i)));
            msg.printer().PrintRaw("\n");
          }
        }},
       {"nested_msgs",
        [&] {
          // If we have no nested types or oneofs, bail out without emitting
          // an empty mod SomeMsg_.
          if (msg.desc().nested_type_count() == 0 &&
              msg.desc().real_oneof_decl_count() == 0) {
            return;
          }
          msg.Emit(
              {{"Msg", msg.desc().name()},
               {"nested_msgs",
                [&] {
                  for (int i = 0; i < msg.desc().nested_type_count(); ++i) {
                    auto nested_msg = msg.WithDesc(msg.desc().nested_type(i));
                    GenerateRs(nested_msg);
                  }
                }},
               {"oneofs",
                [&] {
                  for (int i = 0; i < msg.desc().real_oneof_decl_count(); ++i) {
                    GenerateOneofDefinition(
                        msg.WithDesc(*msg.desc().real_oneof_decl(i)));
                  }
                }}},
              R"rs(
                 #[allow(non_snake_case)]
                 pub mod $Msg$_ {
                   $nested_msgs$

                   $oneofs$
                 }  // mod $Msg$_
                )rs");
        }},
       {"accessor_fns_for_views", [&] { AccessorsForViewOrMut(msg, false); }},
       {"accessor_fns_for_muts", [&] { AccessorsForViewOrMut(msg, true); }}},
      R"rs(
        #[allow(non_camel_case_types)]
        // TODO: Implement support for debug redaction
        #[derive(Debug)]
        pub struct $Msg$ {
          inner: $pbr$::MessageInner
        }

        // SAFETY:
        // - `$Msg$` does not provide shared mutation with its arena.
        // - `$Msg$Mut` is not `Send`, and so even in the presence of mutator
        //   splitting, synchronous access of an arena that would conflict with
        //   field access is impossible.
        unsafe impl Sync for $Msg$ {}

        impl $pb$::Proxied for $Msg$ {
          type View<'a> = $Msg$View<'a>;
          type Mut<'a> = $Msg$Mut<'a>;
        }

        #[derive(Debug, Copy, Clone)]
        #[allow(dead_code)]
        pub struct $Msg$View<'a> {
          msg: $pbi$::RawMessage,
          _phantom: $Phantom$<&'a ()>,
        }

        impl<'a> $Msg$View<'a> {
          #[doc(hidden)]
          pub fn new(_private: $pbi$::Private, msg: $pbi$::RawMessage) -> Self {
            Self { msg, _phantom: std::marker::PhantomData }
          }
          $accessor_fns_for_views$
        }

        // SAFETY:
        // - `$Msg$View` does not perform any mutation.
        // - While a `$Msg$View` exists, a `$Msg$Mut` can't exist to mutate
        //   the arena that would conflict with field access.
        // - `$Msg$Mut` is not `Send`, and so even in the presence of mutator
        //   splitting, synchronous access of an arena is impossible.
        unsafe impl Sync for $Msg$View<'_> {}
        unsafe impl Send for $Msg$View<'_> {}

        impl<'a> $pb$::ViewProxy<'a> for $Msg$View<'a> {
          type Proxied = $Msg$;

          fn as_view(&self) -> $pb$::View<'a, $Msg$> {
            *self
          }
          fn into_view<'shorter>(self) -> $pb$::View<'shorter, $Msg$> where 'a: 'shorter {
            self
          }
        }

        impl<'a> $pb$::SettableValue<$Msg$> for $Msg$View<'a> {
          fn set_on<'b>(self, _private: $pb$::__internal::Private, _mutator: $pb$::Mut<'b, $Msg$>)
          where
            $Msg$: 'b {
            todo!()
          }
        }

        #[derive(Debug)]
        #[allow(dead_code)]
        #[allow(non_camel_case_types)]
        pub struct $Msg$Mut<'a> {
          inner: $pbr$::MutatorMessageRef<'a>,
        }

        impl<'a> $Msg$Mut<'a> {
          #[doc(hidden)]
          pub fn new(_private: $pbi$::Private,
                     parent: &'a mut $pbr$::MessageInner,
                     msg: $pbi$::RawMessage)
            -> Self {
            Self {
              inner: $pbr$::MutatorMessageRef::from_parent(
                       $pbi$::Private, parent, msg)
            }
          }
          $accessor_fns_for_muts$
        }

        // SAFETY:
        // - `$Msg$Mut` does not perform any shared mutation.
        // - `$Msg$Mut` is not `Send`, and so even in the presence of mutator
        //   splitting, synchronous access of an arena is impossible.
        unsafe impl Sync for $Msg$Mut<'_> {}

        impl<'a> $pb$::MutProxy<'a> for $Msg$Mut<'a> {
          fn as_mut(&mut self) -> $pb$::Mut<'_, $Msg$> {
            $Msg$Mut { inner: self.inner }
          }
          fn into_mut<'shorter>(self) -> $pb$::Mut<'shorter, $Msg$> where 'a : 'shorter { self }
        }

        impl<'a> $pb$::ViewProxy<'a> for $Msg$Mut<'a> {
          type Proxied = $Msg$;
          fn as_view(&self) -> $pb$::View<'_, $Msg$> {
            $Msg$View { msg: self.inner.msg(), _phantom: std::marker::PhantomData }
          }
          fn into_view<'shorter>(self) -> $pb$::View<'shorter, $Msg$> where 'a: 'shorter {
            $Msg$View { msg: self.inner.msg(), _phantom: std::marker::PhantomData }
          }
        }

        impl $Msg$ {
          pub fn new() -> Self {
            $Msg::new$
          }

          pub fn serialize(&self) -> $pbr$::SerializedData {
            $Msg::serialize$
          }
          pub fn deserialize(&mut self, data: &[u8]) -> Result<(), $pb$::ParseError> {
            $Msg::deserialize$
          }

          $accessor_fns$

          $oneof_accessor_fns$
        }  // impl $Msg$

        //~ We implement drop unconditionally, so that `$Msg$: Drop` regardless
        //~ of kernel.
        impl $std$::ops::Drop for $Msg$ {
          fn drop(&mut self) {
            $Msg::drop$
          }
        }

        extern "C" {
          $Msg_externs$

          $accessor_externs$

          $oneof_externs$
        }  // extern "C" for $Msg$

        $nested_msgs$
      )rs");

  if (msg.is_cpp()) {
    msg.printer().PrintRaw("\n");
    msg.Emit({{"Msg", msg.desc().name()}}, R"rs(
      impl $Msg$ {
        pub fn __unstable_wrap_cpp_grant_permission_to_break(msg: $pbi$::RawMessage) -> Self {
          Self { inner: $pbr$::MessageInner { msg } }
        }
        pub fn __unstable_cpp_repr_grant_permission_to_break(&mut self) -> $pbi$::RawMessage {
          self.inner.msg
        }
      }
    )rs");
  }
}

// Generates code for a particular message in `.pb.thunk.cc`.
void GenerateThunksCc(Context<Descriptor> msg) {
  ABSL_CHECK(msg.is_cpp());
  if (msg.desc().map_key() != nullptr) {
    ABSL_LOG(WARNING) << "unsupported map field: " << msg.desc().full_name();
    return;
  }

  msg.Emit(
      {{"abi", "\"C\""},  // Workaround for syntax highlight bug in VSCode.
       {"Msg", msg.desc().name()},
       {"QualifiedMsg", cpp::QualifiedClassName(&msg.desc())},
       {"new_thunk", Thunk(msg, "new")},
       {"delete_thunk", Thunk(msg, "delete")},
       {"serialize_thunk", Thunk(msg, "serialize")},
       {"deserialize_thunk", Thunk(msg, "deserialize")},
       {"nested_msg_thunks",
        [&] {
          for (int i = 0; i < msg.desc().nested_type_count(); ++i) {
            Context<Descriptor> nested_msg =
                msg.WithDesc(msg.desc().nested_type(i));
            GenerateThunksCc(nested_msg);
          }
        }},
       {"accessor_thunks",
        [&] {
          for (int i = 0; i < msg.desc().field_count(); ++i) {
            GenerateAccessorThunkCc(msg.WithDesc(*msg.desc().field(i)));
          }
        }},
       {"oneof_thunks",
        [&] {
          for (int i = 0; i < msg.desc().real_oneof_decl_count(); ++i) {
            GenerateOneofThunkCc(msg.WithDesc(*msg.desc().real_oneof_decl(i)));
          }
        }}},
      R"cc(
        //~ $abi$ is a workaround for a syntax highlight bug in VSCode. However,
        //~ that confuses clang-format (it refuses to keep the newline after
        //~ `$abi${`). Disabling clang-format for the block.
        // clang-format off
        extern $abi$ {
        void* $new_thunk$() { return new $QualifiedMsg$(); }
        void $delete_thunk$(void* ptr) { delete static_cast<$QualifiedMsg$*>(ptr); }
        google::protobuf::rust_internal::SerializedData $serialize_thunk$($QualifiedMsg$* msg) {
          return google::protobuf::rust_internal::SerializeMsg(msg);
        }
        bool $deserialize_thunk$($QualifiedMsg$* msg,
                                 google::protobuf::rust_internal::SerializedData data) {
          return msg->ParseFromArray(data.data, data.len);
        }

        $accessor_thunks$

        $oneof_thunks$
        }  // extern $abi$
        // clang-format on

        $nested_msg_thunks$
      )cc");
}

}  // namespace rust
}  // namespace compiler
}  // namespace protobuf
}  // namespace google

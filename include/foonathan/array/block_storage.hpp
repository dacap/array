// Copyright (C) 2018 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef FOONATHAN_ARRAY_BLOCK_STORAGE_ARG_HPP_INCLUDED
#define FOONATHAN_ARRAY_BLOCK_STORAGE_ARG_HPP_INCLUDED

#include <algorithm>
#include <cassert>
#include <tuple>

#include <foonathan/array/block_view.hpp>
#include <foonathan/array/raw_storage.hpp>

#if 0

class BlockStorage
{
public:
    /// Whether or not the block storage may embed some objects inside.
    /// If this is true, the move and swap operations must actually move objects.
    /// If this is false, they will never physically move the objects.
    using embedded_storage = std::integral_constant<bool, ...>;

    /// The arguments required to create the block storage.
    ///
    /// These can be runtime parameters or references to allocators.
    using arg_type = block_storage_args_t<...>;

    //=== constructors/destructors ===//
    /// \effects Creates a block storage with the maximal block size possible without dynamic allocation.
    explicit BlockStorage(arg_type arg) noexcept;

    BlockStorage(const BlockStorage&) = delete;
    BlockStorage& operator=(const BlockStorage&) = delete;

    /// Releases the memory of the block, if it is not empty.
    ~BlockStorage() noexcept;

    /// Swap.
    /// \effects Exchanges ownership over the allocated memory blocks.
    /// When possible this is done without moving the already constructed objects.
    /// If that is not possible, they shall be moved to the beginning of the new location
    /// as if [array::uninitialized_destructive_move]() was used.
    /// The views are updated to view the new location of the constructed objects, if necessary.
    /// \throws Anything thrown by `T`s copy or move constructor.
    /// \notes This function must not allocate dynamic memory.
    template <typename T>
    static void swap(BlockStorage& lhs, block_view<T>& lhs_constructed,
                     BlockStorage& rhs, block_view<T>& rhs_constructed)
                        noexcept(!embedded_storage::value || std::is_nothrow_move_constructible<T>::value);

    //=== reserve/shrink_to_fit ===//
    /// \effects Increases the allocated memory block by at least `min_additional_bytes`.
    /// The range of already created objects is passed as well,
    /// they shall be moved to the beginning of the new location
    /// as if [array::uninitialized_destructive_move]() was used.
    /// \returns A pointer directly after the last constructed object in the new location.
    /// \throws Anything thrown by the allocation function, or the copy/move constructor of `T`.
    /// If an exception is thrown, nothing must have changed.
    /// \notes Use [array::uninitialized_destructive_move]() to move the objects over,
    /// it already provides the strong exception safety for you.
    template <typename T>
    raw_pointer reserve(size_type min_additional_bytes, const block_view<T>& constructed_objects);

    /// \effects Non-binding request to decrease the currently allocated memory block to the minimum needed.
    /// The range of already created objects is passed, those must be moved to the new location like with `reserve()`.
    /// \returns A pointer directly after the last constructed object in the new location.
    /// \throws Anything thrown by the allocation function, or the copy/move constructor of `T`.
    /// If an exception is thrown, nothing must have changed.
    /// \notes Use [array::uninitialized_destructive_move]() to move the objects over,
    /// it already provides the strong exception safety for you.
    template <typename T>
    raw_pointer shrink_to_fit(const block_view<T>& constructed_objects);

    //=== accessors ===//
    /// \returns The currently allocated memory block.
    memory_block block() const noexcept;

    /// \returns The arguments passed to the constructor.
    arg_type arguments() const noexcept;

    /// \returns The maximum size of a memory block managed by this storage,
    /// or `memory_block::max_size()` if there is no limitation by the storage itself.
    size_type max_size() const noexcept;
};

#endif

namespace foonathan
{
    namespace array
    {
        //=== block_storage_args ===//
        /// Tag type to store a collection of arguments to create a `BlockStorage`.
        /// \notes These can be things like runtime parameters or references to allocators.
        template <typename... Args>
        struct block_storage_args_t
        {
            std::tuple<Args...> args;

            /// \effects Creates the default set of arguments,
            /// it may be ill-formed.
            block_storage_args_t() = default;

            /// \effects Creates the arguments from a tuple.
            explicit block_storage_args_t(std::tuple<Args...> args) : args(std::move(args)) {}

            ~block_storage_args_t() noexcept = default;

            /// \effects Copies the arguments, it must not throw.
            block_storage_args_t(const block_storage_args_t&) noexcept = default;

            /// \notes Arguments don't need to be assignable.
            block_storage_args_t& operator=(const block_storage_args_t&) = delete;
        };

        /// \returns The block storage arguments created by forwarding the given arguments to the tuple.
        template <typename... Args>
        block_storage_args_t<typename std::decay<Args>::type...> block_storage_args(
            Args&&... args) noexcept
        {
            return block_storage_args_t<typename std::decay<Args>::type...>(
                std::make_tuple(std::forward<Args>(args)...));
        }

        /// \returns The block storage arguments created by forwarding the given argument to the tuple.
        template <typename Arg>
        block_storage_args_t<typename std::decay<Arg>::type> block_storage_arg(Arg&& arg) noexcept
        {
            return block_storage_args_t<typename std::decay<Arg>::type>(
                std::make_tuple(std::forward<Arg>(arg)));
        }

        //=== BlockStorage algorithms ===//
        /// `std::true_type` if move operations of a `BlockStorage` will never throw, `std::false_type` otherwise.
        ///
        /// They'll never throw if the storage does not use embedded storage or the type is nothrow move constructible.
        template <class BlockStorage, typename T>
        using block_storage_nothrow_move =
            std::integral_constant<bool, !BlockStorage::embbedded_storage::value
                                             || std::is_nothrow_move_constructible<T>::value>;

        /// \effects Clears a block storage by destroying all constructed objects and releasing the memory.
        template <class BlockStorage, typename T>
        void clear_and_shrink(BlockStorage& storage, const block_view<T>& constructed) noexcept
        {
            destroy_range(constructed.begin(), constructed.end());

            BlockStorage empty(storage.arguments());
            BlockStorage::swap(storage, block_view<T>(), empty, block_view<T>());
            // this will never throw as there are no objects that need moving

            // storage now owns no memory
            // empty now owns the memory of storage
            // destructor of empty will release memory of storage
        }

        /// \effects Destroys all created objects and increases the memory block so it has at least `new_size` elements.
        /// \returns A pointer to the new memory.
        template <class BlockStorage, typename T>
        raw_pointer clear_and_reserve(BlockStorage& storage, const block_view<T>& constructed,
                                      size_type new_size)
        {
            destroy_range(constructed.begin(), constructed.end());
            if (new_size <= storage.block().size())
                return storage.block().begin();
            else
                // it returns a pointer one past the last constructed object
                // as no objects are constructed, this will be the beginning of the block
                return storage.reserve(new_size - storage.block().size(), block_view<T>());
        }

        /// \effects Increases the size of the block owned by `dest` to be as big as needed.
        /// then copy constructs or assigns the objects over.
        /// \returns A view to the objects now constructed in `dest`.
        /// \throws Anything thrown by the allocation or copy constructor/assignment of `T`.
        /// \requires The constructed objects must start at the beginning of the memory.
        template <class BlockStorage, typename T, typename FwdIter>
        block_view<T> assign(BlockStorage& dest, block_view<T> dest_constructed, FwdIter begin,
                             FwdIter end)
        {
            static_assert(std::is_convertible<typename std::iterator_traits<FwdIter>::value_type,
                                              T>::value,
                          "type not convertible");

            auto new_size = size_type(std::distance(begin, end));
            auto cur_size = dest_constructed.size();
            if (new_size <= cur_size)
            {
                auto new_end = std::copy(begin, end, dest_constructed.begin());
                destroy_range(new_end, dest_constructed.end());
                return block_view<T>(dest_constructed.begin(), new_end);
            }
            else if (new_size <= dest.block().size())
            {
                auto assign_end = std::next(begin, cur_size);
                std::copy(begin, assign_end, dest_constructed.begin());
                auto new_end =
                    uninitialized_copy_convert<T>(assign_end, end,
                                                  memory_block(as_raw_pointer(
                                                                   dest_constructed.data_end()),
                                                               dest.block().end()));
                return block_view<T>(memory_block(dest.block().begin(), new_end));
            }
            else
            {
                auto new_begin = clear_and_reserve(dest, dest_constructed, new_size);
                auto new_end   = uninitialized_copy(begin, end, dest.block());
                return block_view<T>(memory_block(new_begin, new_end));
            }
        }

        /// \effects Increases the size of the block to be at least `n`,
        /// then fills it by copy constructing/assigning `obj`.
        /// \returns A view to the objects now constructed in `dest`.
        /// \throws Anything thrown by the allocation or copy constructor/assignment of `T`.
        /// \requires The constructed objects must start at the beginning of the memory.
        template <class BlockStorage, typename T>
        block_view<T> fill(BlockStorage& dest, block_view<T> dest_constructed, size_type n,
                           const T& obj)
        {
            auto cur_size = dest_constructed.size();
            if (n <= cur_size)
            {
                auto new_end = std::fill_n(dest_constructed.begin(), std::size_t(n), obj);
                destroy_range(new_end, dest_constructed.end());
                return block_view<T>(dest_constructed.data(), n);
            }
            else if (n <= dest.block().size())
            {
                std::fill_n(dest_constructed.begin(), cur_size, obj);
                auto new_end =
                    uninitialized_fill(memory_block(as_raw_pointer(dest_constructed.data_end()),
                                                    dest.block().end()),
                                       n - cur_size, obj);
                return block_view<T>(memory_block(dest.block().begin(), new_end));
            }
            else
            {
                auto new_begin = clear_and_reserve(dest, dest_constructed, n);
                auto new_end   = uninitialized_fill(dest.block(), n, obj);
                return block_view<T>(memory_block(new_begin, new_end));
            }
        }

        /// Move assignment for block storage.
        /// \effects Transfers ownership of the memory of `other` and objects constructed in it to `dest`,
        /// releasing the memory and objects created in it.
        /// \returns A view on the objects now constructed in `dest`.
        /// It is either the same as `other_constructed` or a view starting at the beginning of the memory now owned by `dest`.
        /// \throws Anything thrown by the copy/move constructor of `T` if actual physical objects need to be moved.
        /// \notes This function will propagate the arguments of the block storage from `other` to `dest`.
        /// This allows taking ownership of the memory allocated by `dest`.
        template <class BlockStorage, typename T>
        block_view<T> move_assign(
            BlockStorage& dest, block_view<T> dest_constructed, BlockStorage&& other,
            block_view<T> other_constructed) noexcept(block_storage_nothrow_move<BlockStorage, T>{})
        {
            // 1. clear the destination
            clear_and_shrink(dest, dest_constructed);
            // dest now owns no memory block

            // 2. swap ownership of the memory blocks
            block_view<T> result;
            BlockStorage::swap(dest, result, other, other_constructed);

            // other is now empty, other_constructed empty view
            // dest now contains the memory of other
            // result views the objects created in that memory
            return result;
        }

        /// Copy assignment for block storage.
        /// \effects Allocates new memory using the arguments from `other` and copies the objects over.
        /// Then changes `dest` to own that memory, releasing previously owned memory.
        /// \returns A view on the objects now constructed in `dest`.
        /// It starts at the beginning of the memory now owned by `dest`.
        /// \throws Anything thrown by the allocation function or copy constructor of `T`.
        /// \notes This function is like [array::assign](), but propagates the arguments of the block storage.
        /// This makes it less efficient as memory of `dest` cannot be reused.
        template <class BlockStorage, typename T>
        block_view<T> copy_assign(BlockStorage& dest, block_view<T> dest_constructed,
                                  const BlockStorage& other, block_view<const T> other_constructed)
        {
            // 1. create a copy of the objects in temporary storage
            BlockStorage temp(other.argument());
            auto         temp_constructed =
                assign(temp, block_view<T>(), other_constructed.begin(), other_constructed.end());
            // only the reserve call can throw, and if it does, nothing has changed

            // 2. swap temp and destination
            BlockStorage::swap(temp, temp_constructed, dest, dest_constructed);
            // temp now owns the memory of dest
            // dest now owns the memory of temp containing the objects in dest_constructed

            return dest_constructed;

            // destructor of temp frees previous memory of dest
        }

        /// Normalizes a block by moving all constructed objects to the front.
        /// \effects Moves the elements currently constructed at `[constructed.begin(), constructed.end())`
        /// to `[storage.block().begin(), storage.block.begin() + constructed.size())`.
        /// \returns A view to the new location of the objects.
        /// \throws Anything thrown by the move constructor or assignment operator.
        template <class BlockStorage, typename T>
        block_view<T> move_to_front(BlockStorage& storage, block_view<T> constructed) noexcept(
            std::is_nothrow_move_constructible<T>::value)
        {
            auto offset = constructed.block().begin() - storage.block().begin();
            assert(offset >= 0);
            if (offset == 0)
                // already at the front
                return constructed;
            else if (offset >= constructed.size())
            {
                // doesn't overlap, just move forward
                auto new_end = uninitialized_destructive_move(constructed.begin(),
                                                              constructed.end(), storage.block());
                return block_view<T>(memory_block(storage.block().begin(), new_end));
            }
            else
            {
                // move construct the first offset elements at the correct location
                auto mid = constructed.begin() + offset;
                uninitialized_move(constructed.begin(), mid, storage.block());

                // now we can assign the next elements to the already moved ones
                auto new_end = std::move(mid, constructed.end(), constructed.begin());

                // destroy the unnecessary trailing elements
                destroy_range(new_end, constructed.end());

                return block_view<T>(to_pointer<T>(storage.block().begin()), constructed.size());
            }
        }
    }
} // namespace foonathan::array

#endif // FOONATHAN_ARRAY_BLOCK_STORAGE_ARG_HPP_INCLUDED

-- DESCRIPTION: Verilator: VHDL record element of an unconstrained array type diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Reading an unconstrained array element of a record has no bounds to lower
-- with, so the frontend must report it instead of failing an internal check
entity t is
  port (
    o : out natural
  );
end entity;

architecture rtl of t is
  type offsets_t is array (natural range <>) of natural;
  type collection_t is record
    offset : offsets_t;
  end record;

  function append(prefix : collection_t) return collection_t is
  begin
    return (offset => prefix.offset & 1);
  end function;

  constant init : collection_t(offset(0 to -1)) := (offset => (others => 0));
  constant fin : collection_t := append(init);
begin
  o <= 0;
end architecture;

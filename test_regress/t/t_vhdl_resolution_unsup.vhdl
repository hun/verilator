-- DESCRIPTION: Verilator: a user resolution function has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;
architecture a of t is
  type bv is array (natural range <>) of bit;
  function wired_or (v : bv) return bit is
  begin
    return '0';
  end function;
  subtype rbit is wired_or bit;
begin
end architecture;

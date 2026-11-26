-- DESCRIPTION: Verilator: VHDL record constraint member with a bound and no direction
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record constraint member takes a range, written with 'to' or 'downto'. Here the member
-- gives one value and no direction.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type rec_t is record
    f : bit_vector(3 downto 0);
  end record;
  signal r : rec_t(f(5));
begin
  o <= '1';
end architecture rtl;

-- DESCRIPTION: Verilator: VHDL 'simple_name of a signal is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- 'simple_name of an object is not lowered.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit_vector(0 to 1);
  signal str : string(1 to 1);
begin
  str <= s'simple_name;
  o <= '1';
end architecture rtl;

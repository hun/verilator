-- DESCRIPTION: Verilator: VHDL index of a qualified expression, not a name
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A qualified expression is not a name, so an element of it has no designator to lower.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal x : bit_vector(1 downto 0);
begin
  o <= bit_vector'(x)(0);
end architecture rtl;

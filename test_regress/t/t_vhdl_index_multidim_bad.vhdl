-- DESCRIPTION: Verilator: VHDL comma index into an array of vectors
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- s is an array of bit vectors, so s(1, 0) indexes a vector element twice.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type ea is array (0 to 3) of bit_vector(1 downto 0);
  signal s : ea;
begin
  o <= s(1, 0);
end architecture rtl;

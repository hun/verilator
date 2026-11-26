-- DESCRIPTION: Verilator: a conditional assignment with several waveform elements is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A waveform list cannot be combined with a conditional.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal a : bit;
begin
  o <= '1', '0' when a = '1' else '0';
end architecture rtl;
